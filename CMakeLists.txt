cmake_minimum_required(VERSION 3.30)

project(LatexCompiler VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)

FetchContent_Declare(
    webview
    GIT_REPOSITORY https://github.com/webview/webview.git
    GIT_TAG 0.12.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(webview)

# The app:// scheme handler must be registered on the WKWebViewConfiguration
# before the WKWebView exists; the library creates both internally, so a small
# idempotent patch exposes a configuration hook. Re-runs are no-ops.
if(APPLE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_webview_hook.py
                "${webview_SOURCE_DIR}/core/include/webview/webview.h"
        OUTPUT_VARIABLE patch_output
        RESULT_VARIABLE patch_result
    )
    if(NOT patch_result EQUAL 0)
        message(FATAL_ERROR "webview hook patch failed: ${patch_output}")
    endif()
    message(STATUS "${patch_output}")
endif()

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
)
FetchContent_MakeAvailable(nlohmann_json)

find_program(PNPM_EXECUTABLE NAMES pnpm REQUIRED)

# macOS serves the frontend from app:// with a real origin, so normal code
# splitting works. Everywhere else the page loads from file:// (null origin),
# where WebKit/Chromium block ES-module chunks: build the single-file bundle.
if(APPLE)
    add_custom_target(frontend-assets
        COMMAND "${PNPM_EXECUTABLE}" --dir "${CMAKE_CURRENT_SOURCE_DIR}/frontend" install
        COMMAND "${PNPM_EXECUTABLE}" --dir "${CMAKE_CURRENT_SOURCE_DIR}/frontend" run build
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Building frontend assets with pnpm"
        VERBATIM
    )
else()
    add_custom_target(frontend-assets
        COMMAND "${PNPM_EXECUTABLE}" --dir "${CMAKE_CURRENT_SOURCE_DIR}/frontend" install
        COMMAND "${CMAKE_COMMAND}" -E env INLINE_BUNDLE=1
                "${PNPM_EXECUTABLE}" --dir "${CMAKE_CURRENT_SOURCE_DIR}/frontend" run build
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Building inline frontend assets with pnpm"
        VERBATIM
    )
endif()

add_library(core STATIC
    src/core/CompileService.cpp
    src/core/EditorService.cpp
    src/core/ProjectPaths.cpp
    src/core/ProjectService.cpp
    src/core/DiagramService.cpp
    src/core/DiagramBuildService.cpp
)
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(core PRIVATE cxx_std_20)
target_link_libraries(core PUBLIC nlohmann_json::nlohmann_json)

add_library(ipc-adapter STATIC
    src/ipc/request_dispatcher.cpp
)
target_include_directories(ipc-adapter
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(ipc-adapter PRIVATE cxx_std_23)
target_link_libraries(ipc-adapter PRIVATE nlohmann_json::nlohmann_json core)

add_library(platform-webview STATIC
    src/platform/webview_window.cpp
    src/platform/webview_config_hook.cpp
)
if(APPLE)
    target_sources(platform-webview PRIVATE
        src/platform/native_bridge.mm
        src/platform/native_menu.mm
        src/adapters/dialog/NativeFolderPicker_mac.mm
        src/adapters/filesystem/SystemFileOpener_mac.mm
        src/adapters/filesystem/SystemEditorLauncher_mac.mm
        src/adapters/notify/MacSystemNotifier_mac.mm
    )
    target_link_libraries(platform-webview PRIVATE "-framework Cocoa" "-framework WebKit" "-framework UserNotifications")
    set_source_files_properties(src/platform/native_bridge.mm
        src/platform/native_menu.mm
        src/adapters/dialog/NativeFolderPicker_mac.mm
        src/adapters/filesystem/SystemFileOpener_mac.mm
        src/adapters/filesystem/SystemEditorLauncher_mac.mm
        src/adapters/notify/MacSystemNotifier_mac.mm PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc")
endif()

if(WIN32)
    target_sources(platform-webview PRIVATE
        src/adapters/dialog/NativeFolderPicker_win.cpp
        src/adapters/filesystem/SystemFileOpener_win.cpp
        src/adapters/filesystem/SystemEditorLauncher_win.cpp
        src/adapters/notify/SystemToastNotifier_win.cpp
    )
    target_link_libraries(platform-webview PRIVATE ole32 shell32)
    # C++/WinRT for ToastNotificationManager; relaxed language standard keeps
    # MSVC happy with the generated WinRT headers.
    set_source_files_properties(src/adapters/notify/SystemToastNotifier_win.cpp PROPERTIES
        COMPILE_OPTIONS "/std:c++latest")
    target_link_libraries(platform-webview PRIVATE WindowsApp)
endif()

if(UNIX AND NOT APPLE)
    target_sources(platform-webview PRIVATE
        src/adapters/dialog/NativeFolderPicker_linux.cpp
        src/adapters/filesystem/SystemEditorLauncher_linux.cpp
        src/adapters/notify/SystemToastNotifier_linux.cpp)
endif()

target_include_directories(platform-webview
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(platform-webview PRIVATE cxx_std_20)
target_link_libraries(platform-webview PRIVATE ipc-adapter webview::core)

add_executable(latex-compiler MACOSX_BUNDLE
    src/app/main.cpp
    src/app/composition_root.cpp
)
target_include_directories(latex-compiler
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(latex-compiler PRIVATE cxx_std_23)
target_link_libraries(latex-compiler PRIVATE platform-webview nlohmann_json::nlohmann_json)
add_dependencies(latex-compiler frontend-assets)

if(MSVC)
    target_compile_options(latex-compiler PRIVATE /W4 /permissive-)
else()
    target_compile_options(latex-compiler PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

set_target_properties(latex-compiler PROPERTIES
    OUTPUT_NAME "Latex Compiler"
    MACOSX_BUNDLE_BUNDLE_NAME "Latex Compiler"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.Misaint20.latexcompiler"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_ICON_FILE AppIcon
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in"
    MACOSX_BUNDLE_INFO_STRING "Latex Compiler ${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "© 2026 Misaint20. Todos los derechos reservados."
)

# Copy the frontend assets on every build. A POST_BUILD on the executable
# would be skipped whenever the link is up to date, leaving a stale bundle.
if(APPLE)
    add_custom_target(bundle-assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/AppIcon.icns"
            "$<TARGET_BUNDLE_CONTENT_DIR:latex-compiler>/Resources/AppIcon.icns"
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_BUNDLE_CONTENT_DIR:latex-compiler>/Resources/frontend"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:latex-compiler>/Resources/frontend"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/frontend"
            "$<TARGET_BUNDLE_CONTENT_DIR:latex-compiler>/Resources/frontend"
        DEPENDS latex-compiler frontend-assets
        COMMENT "Embedding frontend assets in macOS app bundle"
        VERBATIM
    )

    # Ad-hoc signing seals the whole bundle (Info.plist + resources). The
    # linker's automatic signature covers only the executable, leaving the
    # resources unsealed, which makes Notification Center attribution and
    # delivery unreliable. Runs after bundle-assets so the seal is final.
    add_custom_target(sign-bundle ALL
        COMMAND /usr/bin/codesign --force --deep --sign -
                "$<TARGET_BUNDLE_DIR:latex-compiler>"
        DEPENDS bundle-assets
        COMMENT "Ad-hoc signing the app bundle"
        VERBATIM
    )
else()
    # Non-bundle platforms: the entrypoint the binary looks for is
    # <exe_dir>/frontend/index.html (see main.cpp bundle_entrypoint).
    add_custom_target(bundle-assets ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_FILE_DIR:latex-compiler>/frontend"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:latex-compiler>/frontend"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/frontend"
            "$<TARGET_FILE_DIR:latex-compiler>/frontend"
        DEPENDS latex-compiler frontend-assets
        COMMENT "Copying frontend assets next to the executable"
        VERBATIM
    )
endif()

# ---- Packaging (CPack: Windows MSI/ZIP, Linux TGZ; macOS uses DMG script) ----
# Installs the exact runtime layout the binary expects: the executable at
# the root and the single-file frontend in ./frontend (see main.cpp
# bundle_entrypoint).
if(WIN32 OR (UNIX AND NOT APPLE))
    install(TARGETS latex-compiler RUNTIME DESTINATION .)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources/frontend/"
            DESTINATION frontend)
    set(CPACK_PACKAGE_NAME "Latex Compiler")
    set(CPACK_PACKAGE_VENDOR "Misaint20")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
endif()
if(WIN32)
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "LatexCompiler")
    # Start Menu entry (and the base for toast-notification attribution).
    set(CPACK_PACKAGE_EXECUTABLES "Latex Compiler;Latex Compiler")
    # Stable upgrade GUID: identical across versions so a newer MSI upgrades
    # in place instead of stacking installs.
    set(CPACK_WIX_UPGRADE_GUID "c51c878b-4029-461c-b580-bc63934d0929")
    set(CPACK_GENERATOR "WIX;ZIP")
    include(CPack)
endif()
if(UNIX AND NOT APPLE)
    # Portable archive (extract anywhere, run from there). A system-layout
    # DEB would break the exe-relative frontend lookup in /usr/bin.
    string(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" _cpack_system)
    set(CPACK_SYSTEM_NAME "${_cpack_system}")
    set(CPACK_GENERATOR "TGZ")
    # Freedesktop menu entry + hicolor icon. Inside the TGZ they are inert
    # until the user wires them up (documented in the README); the AppImage
    # packaging picks them up automatically from the install tree.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/latexcompiler.desktop"
            DESTINATION share/applications)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/AppIcon.png"
            RENAME "latexcompiler.png"
            DESTINATION share/icons/hicolor/512x512/apps)
    include(CPack)
endif()

include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.12
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(doctest)

enable_testing()
add_executable(core-tests
    tests/main.cpp
    tests/test_compile_service.cpp
    tests/test_compile_history.cpp
    tests/test_recent_projects.cpp
    tests/test_tectonic_output_tracker.cpp
    tests/test_editor_service.cpp
    tests/test_diagram_service.cpp
    tests/test_request_dispatcher.cpp
    tests/test_process_runner.cpp
    tests/test_multi_engine.cpp
    tests/test_diagram_build.cpp
    tests/test_toast_notifier.cpp
)
# Portable Linux/POSIX adapters compile inside core-tests on mac/Linux so
# Linux-only syntax errors surface from any non-Windows host (they cannot
# compile on Windows: <unistd.h> and friends do not exist there).
if(NOT WIN32)
    target_sources(core-tests PRIVATE
        src/adapters/notify/SystemToastNotifier_linux.cpp
        src/adapters/filesystem/SystemFileOpener_linux.cpp
        src/adapters/filesystem/SystemEditorLauncher_linux.cpp
        src/adapters/dialog/NativeFolderPicker_linux.cpp
    )
endif()
target_include_directories(core-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(core-tests PRIVATE cxx_std_20)
target_link_libraries(core-tests PRIVATE core ipc-adapter nlohmann_json::nlohmann_json doctest::doctest)
add_test(NAME core-tests COMMAND core-tests)

add_executable(scan-fixture tests/scan_fixture_main.cpp)
target_include_directories(scan-fixture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(scan-fixture PRIVATE cxx_std_20)

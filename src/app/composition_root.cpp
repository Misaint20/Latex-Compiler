#include "app/composition_root.hpp"
#include "core/MultiEngineCompiler.hpp"
#include "core/DiagramBuildService.hpp"

#include "platform/native_bridge.h"

#include "core/CompileService.hpp"
#include "core/DiagramService.hpp"
#include "core/EditorService.hpp"
#include "core/ProjectService.hpp"

#include "adapters/dialog/NativeFolderPicker.hpp"
#include "adapters/filesystem/LocalFileStore.hpp"
#include "adapters/filesystem/SystemEditorLauncher.hpp"
#include "adapters/filesystem/SystemFileOpener.hpp"
#include "adapters/filesystem/SystemProjectScanner.hpp"
#include "adapters/history/JsonAppSettings.hpp"
#include "adapters/history/JsonCompileHistory.hpp"
#include "adapters/history/JsonRecentProjects.hpp"
#include "adapters/mock/FakeDiagramRenderer.hpp"
#include "adapters/cache/BoundedDiagramCache.hpp"
#include "adapters/notify/MacSystemNotifier.hpp"
#include "adapters/notify/NullNotifier.hpp"
#ifdef _WIN32
#include "adapters/notify/SystemToastNotifier.hpp"
#elif defined(__linux__)
#include "adapters/notify/SystemToastNotifier.hpp"
#endif
#include "adapters/classtex/ClassicTexEngine.hpp"
#include "adapters/process/SystemProcessRunner.hpp"
#include "adapters/tectonic/TectonicCompiler.hpp"
#include "adapters/webview/WebViewEventPublisher.hpp"

#include <iostream>

#ifdef _WIN32
#include <shlobj.h>
#else
#include <cstdlib>
#endif

namespace app {
namespace {

std::filesystem::path home_directory() {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PROFILE, nullptr, 0, path))) {
        return std::filesystem::path(path);
    }
    return {};
#else
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home);
    }
    return {};
#endif
}

} // namespace

std::string default_app_data_dir() {
    const auto home = home_directory();
    if (home.empty()) {
        return {};
    }
#ifdef _WIN32
    return (home / "AppData" / "Local" / "LatexCompiler").generic_string();
#elif defined(__APPLE__)
    return (home / "Library" / "Application Support" / "LatexCompiler").generic_string();
#else
    return (home / ".local" / "share" / "LatexCompiler").generic_string();
#endif
}

Composition compose(const CompositionOptions& options) {
    auto process_runner = std::make_shared<adapters::process::SystemProcessRunner>();
    auto file_store = std::make_shared<adapters::filesystem::LocalFileStore>();
    auto diagram_cache = std::make_shared<adapters::cache::BoundedDiagramCache>();
    auto diagram_renderer = std::make_shared<adapters::mock::FakeDiagramRenderer>();
    auto project_scanner = std::make_shared<adapters::filesystem::SystemProjectScanner>();
    auto folder_picker = std::make_shared<adapters::dialog::NativeFolderPicker>();
    auto file_opener = std::make_shared<adapters::filesystem::SystemFileOpener>();
    const auto storage_root = options.app_data_dir.empty()
                                  ? std::filesystem::path{".latex-compiler"}
                                  : options.app_data_dir;
    auto compile_history = std::make_shared<adapters::history::JsonCompileHistory>(
        file_store, (storage_root / "history").generic_string());
    auto recent_projects = std::make_shared<adapters::history::JsonRecentProjects>(
        file_store, storage_root.generic_string());
    auto app_settings = std::make_shared<adapters::history::JsonAppSettings>(
        file_store, storage_root.generic_string());

    // Multi-engine compile chain: classic TeX Live engines first (auto),
    // Tectonic as fallback; the persisted preference can pin one engine.
    auto classic_engine = std::make_shared<adapters::classtex::ClassicTexEngine>(process_runner);
    auto tectonic_engine = std::make_shared<adapters::tectonic::TectonicCompiler>(process_runner);
    auto compiler = std::make_shared<core::MultiEngineCompiler>(
        std::vector<core::MultiEngineCompiler::Entry>{
            {"classic", classic_engine},
            {"tectonic", tectonic_engine},
        },
        app_settings);
    auto editor_launcher = std::make_shared<adapters::filesystem::SystemEditorLauncher>();

    // Diagram pre-build: the .mmd sources are the single source of truth;
    // the demanded SVG/PDF/PNG artifacts are regenerated before each compile.
    auto diagram_builder = std::make_shared<core::DiagramBuildService>(process_runner);

    // The webview publisher buffers events until the frontend signals that it
    // is listening; the platform window flips the flag via the ready hook.
    auto event_publisher = std::make_shared<adapters::webview::WebViewEventPublisher>(nullptr);
    auto event_publisher_raw = event_publisher.get();

    auto compile_service = std::make_shared<core::CompileService>(
        compiler, file_store, event_publisher, file_opener, compile_history);
    compile_service->setDiagramBuilder(diagram_builder);
    // OS notifications when a compile finishes while the user is elsewhere.
#ifdef _WIN32
    auto notifier = std::make_shared<adapters::notify::SystemToastNotifier>(process_runner);
#elif defined(__APPLE__)
    auto notifier = std::make_shared<adapters::notify::MacSystemNotifier>();
#elif defined(__linux__)
    auto notifier = std::make_shared<adapters::notify::SystemToastNotifier>(process_runner);
#else
    auto notifier = std::make_shared<adapters::notify::NullNotifier>();
#endif
    compile_service->setNotifier(notifier);
    auto project_service = std::make_shared<core::ProjectService>(
        project_scanner, folder_picker, file_store, file_opener, recent_projects);
    auto editor_service = std::make_shared<core::EditorService>(
        app_settings, editor_launcher, event_publisher);
    project_service->setEditorService(editor_service);
    auto diagram_service = std::make_shared<core::DiagramService>(
        diagram_renderer, diagram_cache);

    auto dispatcher = std::make_shared<ipc::RequestDispatcher>(
        compile_service, project_service, diagram_service, editor_service, app_settings);
    // Stored preferences (notification toggle, ...) gate services at startup.
    dispatcher->applyStoredPreferences();

#if defined(__APPLE__)
    dispatcher->setProjectMenuEnabler([](bool enabled) {
        platform_set_project_menu_enabled(enabled);
    });
    dispatcher->setNativeMenuLanguageSetter([](const std::string& language) {
        platform_set_native_menu_language(language);
    });
#endif

    Composition composition;
    composition.dispatcher = dispatcher;
    composition.app_data_dir = options.app_data_dir;
    composition.on_webview_ready = [event_publisher_raw]() {
        event_publisher_raw->setReady(true);
    };
    composition.on_window_detach = [event_publisher_raw]() {
        event_publisher_raw->setReady(false);
    };
    return composition;
}

} // namespace app

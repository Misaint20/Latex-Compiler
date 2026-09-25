#ifndef PLATFORM_NATIVE_MENU_H
#define PLATFORM_NATIVE_MENU_H

#include <functional>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif

// Builds the native application menu (App, File, Go, Edit, Language) and
// installs it on the NSApplication main menu. On macOS these menus render in
// the system menu bar, not inside the window. No-op on other platforms.
//
// `on_command` receives ("menu.openFolder", "") or ("menu.goTo", section) and
// must be safe to call from the main thread (the platform window forwards the
// payload into the webview via dispatch).
void platform_install_native_menu(
    const std::function<void(const std::string&, const std::string&)>& on_command);

// Retitles the system menu bar to the app language ("Archivo", "File", ...).
// Section keys are stable identifiers; labels come from the table above.
void platform_set_native_menu_language(const std::string& language);

// Enables/disables the project-scoped menu items (Go submenu). Called from the
// frontend-driven project state so menu entries are only actionable while a
// project is open. Safe to call from any thread.
void platform_set_project_menu_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif // PLATFORM_NATIVE_MENU_H

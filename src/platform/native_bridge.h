#ifndef PLATFORM_NATIVE_BRIDGE_H
#define PLATFORM_NATIVE_BRIDGE_H

#include <functional>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif

// WKWebView refuses to load relative subresources of a file:// page loaded via
// loadRequest:. This shim uses loadFileURL:allowingReadAccessToURL: instead,
// which is the only reliable way to serve a local SPA bundle on macOS.
void platform_load_bundle_url(void* webview_widget, const char* html_file_path);

// Directory root served under app:// by the scheme handler.
void platform_set_assets_root(const char* path);

// Adapts the native window so the web content extends under the title bar
// with the traffic-light buttons overlaying the app chrome. The page draws
// its own top bar and reserves drag space. Receives the bare NSWindow
// pointer; a no-op when called on other platforms.
void platform_integrate_title_bar(void* ns_window);

// Builds the native application menu. On macOS it lives in the system menu
// bar; other platforms render their own in-window menu bar and this is a
// no-op. Commands arrive as ("menu.openFolder", "") or ("menu.goTo", section).
void platform_install_native_menu(
    const std::function<void(const std::string&, const std::string&)>& on_command);

// Enables the project-scoped menu entries while a project is open.
void platform_set_project_menu_enabled(bool enabled);

// Retitles the native menu bar to the app language; no-op on other platforms.
void platform_set_native_menu_language(const std::string& language);

// Called by the patched webview library right after it creates the
// WKWebViewConfiguration and before the WKWebView exists. The hook registers
// the app:// URL scheme handler there, which is the only moment Apple allows
// it. Receives the bare WKWebViewConfiguration pointer.
void platform_on_webview_configuration(void* wk_webview_configuration);

#ifdef __cplusplus
}
#endif

#endif // PLATFORM_NATIVE_BRIDGE_H

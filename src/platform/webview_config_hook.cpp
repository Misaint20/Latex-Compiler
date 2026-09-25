// The app:// URL scheme handler hook only exists in the Apple (cocoa) engine
// of the patched webview library, so this TU is meaningful on macOS alone. It
// still compiles on every platform — empty elsewhere — to keep the target
// sources uniform. The only caller (webview_window.cpp) guards the call with
// the same condition.
#if defined(__APPLE__)

#include "platform/native_bridge.h"

// The webview library header must be included as pure C++ (it casts between
// void* and ObjC pointers without bridging), so the hook assignment lives in
// this .cpp instead of native_bridge.mm.
#include <webview/webview.h>

namespace platform {
namespace {

extern "C" void platform_on_webview_configuration(void* wk_webview_configuration);

} // namespace

void assign_webview_config_hook() {
    webview::detail::config_hook = &platform_on_webview_configuration;
}

} // namespace platform

#endif // __APPLE__

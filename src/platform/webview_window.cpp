#include "platform/webview_window.hpp"

#include "platform/native_bridge.h"
#include "ipc/request_dispatcher.hpp"

#include <webview/webview.h>

#include <cctype>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <functional>
#include <memory>
#include <utility>

namespace {

std::string file_url(const std::filesystem::path& file) {
    const auto path = std::filesystem::absolute(file).generic_string();
    std::ostringstream url;
    // Windows drive paths (C:/...) need file:/// — otherwise the drive
    // letter parses as a URL host.
    const bool windows_drive = path.size() > 1 && path[1] == ':';
    url << (windows_drive ? "file:///" : "file://");

    for (const char character : path) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0 || character == '-' || character == '.' ||
            character == '_' || character == '~' || character == '/') {
            url << character;
        } else {
            url << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(byte) << std::nouppercase << std::dec;
        }
    }

    return url.str();
}

bool is_http_url(const std::string& url) {
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

void run_when_page_ready(webview::webview& window, const std::function<void()>& callback) {
    // The library exposes no page-load callback here. The webview init script
    // installs the bridge before page scripts run, so a short delayed dispatch
    // is enough for the frontend to register its event listener in time. A
    // detached thread only sleeps; it never competes for CPU.
    std::thread([callback, &window]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        window.dispatch([callback]() { callback(); });
    }).detach();
}

} // namespace

namespace platform {

#if defined(__APPLE__)
// The patched webview library calls this with the bare WKWebViewConfiguration
// while it creates its engine; registering the app:// scheme handler here is
// the only moment Apple allows it. Defined in native_bridge.mm.
void assign_webview_config_hook();
#endif

int run_frontend_window(WindowOptions options, std::shared_ptr<ipc::RequestDispatcher> dispatcher) {
#if defined(__APPLE__)
    assign_webview_config_hook();
#endif
    webview::webview window{true, nullptr};
    window.set_title("LaTeX Compiler");
    window.set_size(1280, 800, WEBVIEW_HINT_NONE);

#if defined(__APPLE__)
    // Content flows under the native title bar; the page top bar reserves a
    // drag strip so the traffic-light buttons overlay the app chrome. The
    // window background drag plus the page's -webkit-app-region CSS handle
    // window dragging.
    const auto native_window = window.window();
    if (native_window.ok()) {
        platform_integrate_title_bar(native_window.value());
    }
#endif

#if defined(__APPLE__)
    // The macOS menu bar lives outside the window: File opens the picker, Go
    // jumps between project sections, Edit wires clipboard shortcuts. Commands
    // reach the page as ordinary native events on the UI thread.
    platform_install_native_menu([&window](const std::string& topic, const std::string& payload) {
        window.dispatch([&window, topic, payload]() {
            std::string js = "window.__emitNativeEvent && window.__emitNativeEvent(";
            js += webview::detail::json_escape(topic);
            js += ", ";
            js += webview::detail::json_escape(payload);
            js += ");";
            window.eval(js);
        });
    });
#endif

    // Async bridge: the binding returns immediately so the UI main queue
    // never blocks on disk, dialogs or process work. Replies come back through
    // the reply sink, which re-enters the UI thread via webview::dispatch and
    // resolves the JS Promise with the library's onReply using the binding id
    // the wrapper handed us.
    //
    // The library's on_message discards the binding callback's return value,
    // so EVERY reply (fast and slow handlers alike) must travel through the
    // reply sink; the sync dispatch result is intentionally ignored here.
    const auto bind_result = window.bind(
        "latexCompiler",
        [dispatcher](const std::string& binding_id, const std::string& req, void* /*arg*/) {
            dispatcher->dispatch(req, binding_id);
        },
        nullptr);
    if (!bind_result.ok() &&
        bind_result.error().code() != WEBVIEW_ERROR_DUPLICATE) {
        std::cerr << "Failed to register the native bridge binding." << std::endl;
        return 1;
    }

    dispatcher->setReplySink([&window](std::string binding_id, std::string response_json) {
        window.dispatch([&window, id = std::move(binding_id),
                         response = std::move(response_json)]() {
            std::string js = "window.__webview__.onReply(";
            js += webview::detail::json_escape(id);
            js += ", 0, ";
            js += webview::detail::json_escape(response);
            js += ");";
            window.eval(js);
        });
    });

    // Empty-string replies mean the async wrapper already completed the
    // request through the reply sink; forward everything else so fast
    // handlers (scan, ping, status, settings...) resolve their Promises too.
    dispatcher->setSyncReplySink([&window](std::string binding_id, std::string response_json) {
        if (binding_id.empty() || response_json.empty()) {
            return;
        }
        window.dispatch([&window, id = std::move(binding_id),
                         response = std::move(response_json)]() {
            std::string js = "window.__webview__.onReply(";
            js += webview::detail::json_escape(id);
            js += ", 0, ";
            js += webview::detail::json_escape(response);
            js += ");";
            window.eval(js);
        });
    });

    bool loaded = false;
    if (!options.dev_server_url.empty() && is_http_url(options.dev_server_url)) {
        std::cout << "Frontend dev server: " << options.dev_server_url << std::endl;
        window.navigate(options.dev_server_url);
        loaded = true;
    } else if (std::filesystem::exists(options.bundle_entrypoint)) {
        const auto entrypoint = std::filesystem::absolute(options.bundle_entrypoint);
#if defined(__APPLE__)
        // The frontend is served from app:// with a real origin, so ES module
        // chunks load normally and lazy imports work.
        platform_set_assets_root(
            entrypoint.parent_path().generic_string().c_str());
        window.navigate("app://localhost/index.html");
        loaded = true;
#else
        window.navigate(file_url(entrypoint));
        loaded = true;
#endif
    } else {
        std::cerr << "Frontend bundle not found at: "
                  << std::filesystem::absolute(options.bundle_entrypoint).generic_string()
                  << std::endl;
        window.set_html("<h1>Frontend assets were not found.</h1>");
    }

    if (loaded && options.on_ready) {
        run_when_page_ready(window, options.on_ready);
    }

    window.run();

    if (options.on_detach) {
        options.on_detach();
    }
    return 0;
}

} // namespace platform

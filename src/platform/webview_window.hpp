#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace ipc {
class RequestDispatcher;
}

namespace platform {

struct WindowOptions {
    std::filesystem::path bundle_entrypoint;
    std::string dev_server_url;
    // Directory served under app:// on platforms with a scheme handler.
    std::filesystem::path assets_dir;
    // Called once the webview is ready to receive JS evaluations.
    std::function<void()> on_ready;
    // Called right before the window loop exits.
    std::function<void()> on_detach;
};

int run_frontend_window(WindowOptions options, std::shared_ptr<ipc::RequestDispatcher> dispatcher);

} // namespace platform

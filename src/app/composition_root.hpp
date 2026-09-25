#pragma once

#include "ipc/request_dispatcher.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace app {

struct CompositionOptions {
    std::filesystem::path app_data_dir;
};

struct Composition {
    std::shared_ptr<ipc::RequestDispatcher> dispatcher;
    std::filesystem::path app_data_dir;
    // Optional hook invoked once the webview can receive JS evaluations.
    std::function<void()> on_webview_ready;
    // Optional hook invoked when the window loop is about to exit.
    std::function<void()> on_window_detach;
};

Composition compose(const CompositionOptions& options);

std::string default_app_data_dir();

} // namespace app

#include "app/composition_root.hpp"
#include "platform/webview_window.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

struct LaunchOptions {
    std::string dev_server_url;
};

LaunchOptions parse_arguments(int argc, char* argv[]) {
    LaunchOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--dev-url" && i + 1 < argc) {
            options.dev_server_url = argv[++i];
        }
    }
    return options;
}

std::filesystem::path executable_path() {
#ifdef __APPLE__
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    return std::filesystem::canonical(buffer.c_str());
#elif defined(_WIN32)
    // /proc/self/exe does not exist on Windows; the handle-based query is
    // the canonical way and survives paths with spaces and Unicode.
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD size = 0;
    while (true) {
        size = GetModuleFileNameW(nullptr, buffer.data(),
                                  static_cast<DWORD>(buffer.size()));
        if (size == 0) {
            return {};
        }
        if (size < buffer.size() - 1) {
            break;
        }
        // Truncated: retry with a larger buffer.
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::canonical(buffer.data());
#else
    return std::filesystem::canonical("/proc/self/exe");
#endif
}

std::filesystem::path bundle_entrypoint() {
    const auto executable = executable_path();
    if (executable.empty()) {
        return {};
    }
#ifdef __APPLE__
    return executable.parent_path().parent_path() / "Resources" / "frontend" / "index.html";
#else
    return executable.parent_path() / "frontend" / "index.html";
#endif
}

} // namespace

int main(int argc, char* argv[]) {
    const auto options = parse_arguments(argc, argv);
    const auto app_data = app::default_app_data_dir();

    const auto composition = app::compose({app_data});
    platform::WindowOptions window_options;
    window_options.bundle_entrypoint = bundle_entrypoint();
    window_options.assets_dir = bundle_entrypoint().parent_path();
    window_options.dev_server_url = options.dev_server_url;
    window_options.on_ready = composition.on_webview_ready;
    window_options.on_detach = composition.on_window_detach;

    return platform::run_frontend_window(window_options, composition.dispatcher);
}

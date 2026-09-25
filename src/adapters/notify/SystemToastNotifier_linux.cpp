#include "adapters/notify/SystemToastNotifier.hpp"

#include "core/contracts/IProcessRunner.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace adapters {
namespace notify {
namespace {

// Click-to-open parity with macOS: notify-send (libnotify >= 0.7.7) can wait
// for user interaction and report the chosen action on stdout. Blocking the
// compile worker for that span is safe — the job already finished, and the
// service's cancel path kills the child when a new compile starts.
constexpr const char* kOpenAction = "open=Open PDF";

} // namespace

SystemToastNotifier::SystemToastNotifier(
    std::shared_ptr<core::contracts::IProcessRunner> runner)
    : runner_(std::move(runner)) {}

void SystemToastNotifier::notify(const std::string& title, const std::string& body,
                                 const std::string& open_path) {
    if (!runner_) {
        return;
    }

    core::contracts::ProcessRequest request;
    // Args pass directly (no shell), so spaces and quotes in titles are safe.
    request.command = "notify-send";
    request.args = {"-a", "Latex Compiler"};
    if (!open_path.empty()) {
        request.args.push_back("--action");
        request.args.push_back(kOpenAction);
        request.args.push_back("--wait");
    }
    request.args.push_back(title);
    request.args.push_back(body);
    request.timeout_seconds = open_path.empty() ? 10 : 600;

    std::string stdout_capture;
    const auto result = runner_->run(request, [&stdout_capture](const char* data, std::size_t size) {
        stdout_capture.append(data, size);
    });
    if (result.timed_out) {
        return;
    }

    // The user clicked "Open PDF": reveal the compiled document.
    if (stdout_capture.find("open") != std::string::npos && !open_path.empty()) {
        core::contracts::ProcessRequest open_request;
        open_request.command = "xdg-open";
        open_request.args = {open_path};
        open_request.timeout_seconds = 15;
        (void)runner_->run(open_request);
    }
}

} // namespace notify
} // namespace adapters

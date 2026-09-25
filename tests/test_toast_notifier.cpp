// Covers the notify-send adapter: correct tool, shell-free argv, and
// click-to-open parity with macOS via the process runner.
#include "adapters/notify/SystemToastNotifier.hpp"
#include "core/contracts/IProcessRunner.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

namespace {

class RecordingRunner : public core::contracts::IProcessRunner {
public:
    core::contracts::ProcessResult run(const core::contracts::ProcessRequest& request) override {
        return run(request, {});
    }

    core::contracts::ProcessResult run(const core::contracts::ProcessRequest& request,
                                       const core::contracts::OutputSink& sink) override {
        commands.push_back(request.command);
        args.push_back(request.args);
        if (sink) {
            sink(action_output.data(), action_output.size());
        }
        return {0, action_output, {}, false};
    }

    void cancelActive() override {}

    std::vector<std::string> commands;
    std::vector<std::vector<std::string>> args;
    std::string action_output;
};

bool has_arg(const std::vector<std::string>& argv, const std::string& needle) {
    for (const auto& arg : argv) {
        if (arg == needle) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("Linux toast with open path requests the click action and waits") {
    auto runner = std::make_shared<RecordingRunner>();
    adapters::notify::SystemToastNotifier notifier(runner);

    notifier.notify("Compile finished", "AXON.pdf", "/tmp/proj/AXON.pdf");

    REQUIRE(runner->commands.size() == 1);
    CHECK(runner->commands[0] == "notify-send");
    // Title and body pass as direct argv (no shell quoting involved).
    REQUIRE(runner->args[0].size() >= 2);
    CHECK(has_arg(runner->args[0], "--action"));
    CHECK(has_arg(runner->args[0], "--wait"));
    // No click in the fake: no xdg-open spawn.
    CHECK(runner->commands.size() == 1);
}

TEST_CASE("Linux toast click reports back and opens the document") {
    auto runner = std::make_shared<RecordingRunner>();
    runner->action_output = "open\n";
    adapters::notify::SystemToastNotifier notifier(runner);

    notifier.notify("Compile finished", "AXON.pdf", "/tmp/proj/AXON.pdf");

    REQUIRE(runner->commands.size() == 2);
    CHECK(runner->commands[1] == "xdg-open");
    REQUIRE(runner->args[1].size() == 1);
    CHECK(runner->args[1][0] == "/tmp/proj/AXON.pdf");
}

TEST_CASE("Failure notifications (no open path) skip the blocking wait") {
    auto runner = std::make_shared<RecordingRunner>();
    adapters::notify::SystemToastNotifier notifier(runner);

    notifier.notify("Compile failed", "Undefined control sequence", "");

    REQUIRE(runner->commands.size() == 1);
    CHECK_FALSE(has_arg(runner->args[0], "--wait"));
    CHECK_FALSE(has_arg(runner->args[0], "--action"));
}

TEST_CASE("Toast notification without a runner stays silent") {
    adapters::notify::SystemToastNotifier notifier(nullptr);
    // Must not crash; there is nothing to assert beyond survival.
    notifier.notify("Compile finished", "x", "y");
}

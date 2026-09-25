#pragma once

#include <functional>
#include <string>
#include <vector>

namespace core {
namespace contracts {

struct ProcessRequest {
    std::string command;
    std::vector<std::string> args;
    std::string working_directory;
    int timeout_seconds = 0;
};

struct ProcessResult {
    int exit_code = -1;
    std::string std_out;
    std::string std_err;
    bool timed_out = false;
};

using OutputSink = std::function<void(const char* data, std::size_t size)>;

class IProcessRunner {
public:
    virtual ~IProcessRunner() = default;

    virtual ProcessResult run(const ProcessRequest& request) = 0;

    // Streaming variant: the sink receives output as the child produces it.
    // The default implementation falls back to buffered delivery after exit.
    virtual ProcessResult run(const ProcessRequest& request, const OutputSink& sink) {
        auto result = run(request);
        if (sink && !result.std_out.empty()) {
            sink(result.std_out.data(), result.std_out.size());
        }
        return result;
    }

    // Terminates the currently running child, if any. Safe to call from any
    // thread while run() is blocked.
    virtual void cancelActive() {}
};

} // namespace contracts
} // namespace core

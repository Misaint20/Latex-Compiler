#pragma once

#include "core/contracts/IProcessRunner.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace adapters {
namespace process {

// Runs child processes with a working directory and timeout, streaming output
// chunks to the caller as the child produces them. Cancellation kills the
// active child from another thread; state is guarded by a mutex so concurrent
// cancel requests cannot race with process bookkeeping.
class SystemProcessRunner : public core::contracts::IProcessRunner {
public:
    core::contracts::ProcessResult run(const core::contracts::ProcessRequest& request) override {
        return run(request, nullptr);
    }

    core::contracts::ProcessResult run(const core::contracts::ProcessRequest& request,
                                       const core::contracts::OutputSink& sink) override {
#ifdef _WIN32
        return run_windows(request, sink);
#else
        return run_posix(request, sink);
#endif
    }

    void cancelActive() override {
        std::lock_guard<std::mutex> lock(state_mutex_);
        cancel_requested_ = true;
#ifdef _WIN32
        active_process_ = nullptr;
#else
        active_child_ = 0;
#endif
    }

private:
    std::mutex state_mutex_;
    std::atomic<bool> cancel_requested_{false};
#ifdef _WIN32
    HANDLE active_process_ = nullptr;
#else
    pid_t active_child_ = 0;
#endif

    bool consume_cancel() {
        return cancel_requested_.exchange(false);
    }

#ifdef _WIN32
    core::contracts::ProcessResult run_windows(const core::contracts::ProcessRequest& request,
                                               const core::contracts::OutputSink& sink) {
        core::contracts::ProcessResult result;
        std::string command_line = quote(request.command);
        for (const auto& arg : request.args) {
            command_line += " " + quote(arg);
        }

        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;

        HANDLE out_read = nullptr;
        HANDLE out_write = nullptr;
        if (CreatePipe(&out_read, &out_write, &security, 0) == FALSE) {
            result.std_err = "Failed to create output pipe";
            return result;
        }
        SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = out_write;
        startup.hStdError = out_write;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

        PROCESS_INFORMATION process{};
        std::vector<char> mutable_command(command_line.begin(), command_line.end());
        mutable_command.push_back('\0');

        const BOOL created = CreateProcessA(
            nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr,
            request.working_directory.empty() ? nullptr : request.working_directory.c_str(),
            &startup, &process);
        CloseHandle(out_write);
        if (created == FALSE) {
            CloseHandle(out_read);
            result.std_err = "Failed to launch: " + command_line;
            return result;
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            active_process_ = process.hProcess;
        }

        std::string output;
        std::array<char, 65536> buffer{};
        DWORD bytes_read = 0;
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(std::max(request.timeout_seconds, 0));
        for (;;) {
            while (ReadFile(out_read, buffer.data(), static_cast<DWORD>(buffer.size()),
                            &bytes_read, nullptr) &&
                   bytes_read > 0) {
                output.append(buffer.data(), bytes_read);
                if (sink) {
                    sink(buffer.data(), static_cast<std::size_t>(bytes_read));
                }
            }
            if (bytes_read == 0) {
                break;
            }
            if (consume_cancel()) {
                TerminateProcess(process.hProcess, 1);
                result.timed_out = true;
                break;
            }
            if (request.timeout_seconds > 0 &&
                std::chrono::steady_clock::now() > deadline) {
                TerminateProcess(process.hProcess, 1);
                result.timed_out = true;
                break;
            }
            Sleep(10);
        }
        CloseHandle(out_read);

        DWORD exit_code = 0;
        WaitForSingleObject(process.hProcess, INFINITE);
        GetExitCodeProcess(process.hProcess, &exit_code);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            active_process_ = nullptr;
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);

        result.exit_code = static_cast<int>(exit_code);
        result.std_out = output;
        return result;
    }

    static std::string quote(const std::string& arg) {
        std::string quoted = "\"";
        for (const char character : arg) {
            if (character == '"') {
                quoted += "\\\"";
            } else {
                quoted += character;
            }
        }
        return quoted + "\"";
    }
#else
    core::contracts::ProcessResult run_posix(const core::contracts::ProcessRequest& request,
                                             const core::contracts::OutputSink& sink) {
        core::contracts::ProcessResult result;

        int pipe_fds[2];
        if (pipe(pipe_fds) != 0) {
            result.std_err = "Failed to create output pipe";
            return result;
        }

        const pid_t child = fork();
        if (child < 0) {
            close(pipe_fds[0]);
            close(pipe_fds[1]);
            result.std_err = "Failed to fork process";
            return result;
        }

        if (child == 0) {
            close(pipe_fds[0]);
            dup2(pipe_fds[1], STDOUT_FILENO);
            dup2(pipe_fds[1], STDERR_FILENO);
            close(pipe_fds[1]);

            if (!request.working_directory.empty() &&
                chdir(request.working_directory.c_str()) != 0) {
                _exit(127);
            }

            std::vector<char*> argv;
            argv.reserve(request.args.size() + 2);
            argv.push_back(const_cast<char*>(request.command.c_str()));
            for (const auto& arg : request.args) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);

            execvp(request.command.c_str(), argv.data());
            _exit(127);
        }

        close(pipe_fds[1]);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            active_child_ = child;
        }

        std::string output;
        std::array<char, 65536> buffer{};
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(std::max(request.timeout_seconds, 0));
        bool canceled = false;
        bool timed_out = false;
        for (;;) {
            const ssize_t bytes = read(pipe_fds[0], buffer.data(), buffer.size());
            if (bytes < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (bytes == 0) {
                break;
            }
            output.append(buffer.data(), static_cast<std::size_t>(bytes));
            if (sink) {
                sink(buffer.data(), static_cast<std::size_t>(bytes));
            }
            if (consume_cancel()) {
                kill(child, SIGKILL);
                canceled = true;
                break;
            }
            if (request.timeout_seconds > 0 &&
                std::chrono::steady_clock::now() > deadline) {
                kill(child, SIGKILL);
                timed_out = true;
                break;
            }
        }
        close(pipe_fds[0]);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            active_child_ = 0;
        }

        int status = 0;
        if (waitpid(child, &status, 0) < 0) {
            result.exit_code = -1;
        } else if (WIFEXITED(status)) {
            result.exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            result.exit_code = 128 + WTERMSIG(status);
        }

        result.timed_out = timed_out;
        if (canceled) {
            result.std_err = "Compilation canceled.";
        }
        result.std_out = output;
        return result;
    }
#endif
};

} // namespace process
} // namespace adapters

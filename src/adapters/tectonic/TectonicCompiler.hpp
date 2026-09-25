#pragma once

#include "TectonicOutputTracker.hpp"
#include "core/contracts/ICompiler.hpp"
#include "core/contracts/IProcessRunner.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace adapters {
namespace tectonic {

class TectonicCompiler : public core::contracts::ICompiler {
public:
    explicit TectonicCompiler(std::shared_ptr<core::contracts::IProcessRunner> runner)
        : runner_(std::move(runner)) {}

    std::vector<core::contracts::EngineInfo> availableEngines() override {
        std::error_code ec;
        const bool installed = std::filesystem::is_regular_file(resolveTectonicPath(), ec);
        return {{"tectonic", "Tectonic", installed}};
    }

    core::contracts::CompileResult compile(const core::contracts::CompileRequest& request) override {
        return compile(request, nullptr);
    }

    core::contracts::CompileResult compile(const core::contracts::CompileRequest& request,
                                           core::contracts::ICompileProgress* progress) override {
        namespace fs = std::filesystem;

        const fs::path project_root(request.projectPath);
        const fs::path main_file(request.mainFile);
        std::error_code ec;
        if (request.projectPath.empty() || !fs::is_directory(project_root, ec)) {
            return finish(progress, {false, {}, "Project path does not exist: " + request.projectPath});
        }

        core::contracts::ProcessRequest process_request;
        process_request.command = resolveTectonicPath();
        process_request.args = {"-X", "compile", main_file.generic_string()};
        process_request.working_directory = project_root.generic_string();
        process_request.timeout_seconds = 300;        core::contracts::OutputSink sink;
        TectonicOutputTracker tracker;
        if (progress != nullptr) {
            // Complete lines are forwarded with their newline so the tracker
            // sees whole lines; a trailing partial waits for the next chunk.
            sink = [progress, &tracker, line_buffer = std::string{}](const char* data,
                                                                     std::size_t size) mutable {
                const std::string_view chunk(data, size);
                std::size_t start = 0;
                while (start < chunk.size()) {
                    const std::size_t newline = chunk.find('\n', start);
                    if (newline == std::string_view::npos) {
                        line_buffer.append(chunk.substr(start));
                        return;
                    }
                    line_buffer.append(chunk.substr(start, newline - start));
                    line_buffer.push_back('\n');
                    progress->onOutput(line_buffer);
                    const auto sample = tracker.feed(line_buffer);
                    if (!sample.stage_key.empty() || !sample.file.empty()) {
                        core::contracts::ProgressInfo info;
                        info.percent = tracker.percent();
                        info.stage = tracker.stage();
                        info.stage_key = tracker.stage_key();
                        info.file = tracker.file();
                        progress->onProgress(info);
                    }
                    line_buffer.clear();
                    start = newline + 1;
                }
            };
        }

        const auto process_result = runner_->run(process_request, sink);
        if (process_result.timed_out) {
            return finish(progress, {false, {}, "Compilation timed out."});
        }
        if (process_result.exit_code < 0 || (process_result.exit_code == 128 + SIGKILL_CODE)) {
            return finish(progress, {false, {}, "Compilation canceled."});
        }
        if (process_result.exit_code != 0) {
            std::string message = process_result.std_out;
            if (!process_result.std_err.empty()) {
                message += (message.empty() ? "" : "\n") + process_result.std_err;
            }
            if (process_result.exit_code == 127 && message.find("not found") != std::string::npos) {
                message = "Tectonic executable was not found on PATH.";
            }
            if (request.engine == "tectonic" && message.find("Tectonic executable was not found") != std::string::npos) {
                // Auto mode surfaces this as the engine being unavailable so
                // the multi-engine selector can try the remaining engines.
                message = "Engine 'tectonic' was not found on this system.";
            }
            return finish(progress, {false, {}, message});
        }

        const auto pdf = find_output_pdf(project_root, main_file);
        if (!pdf) {
            return finish(progress, {false, {}, "Compilation finished but no PDF was produced."});
        }

        return finish(progress, {true, pdf->generic_string(), {}});
    }

    void cancel(core::contracts::CompileId) override {
        runner_->cancelActive();
    }

private:
    static constexpr int SIGKILL_CODE = 9;

    // Resolves the tectonic binary once per process. Checking PATH manually
    // avoids paying a shell+which spawn on every compile and lets us report a
    // precise error instead of exit 127.
    static const std::string& resolveTectonicPath() {
        static const std::string cached = locateTectonic();
        return cached;
    }

    static std::string locateTectonic() {
        namespace fs = std::filesystem;
        if (const char* override_path = std::getenv("LATEX_COMPILER_TECTONIC")) {
            if (override_path[0] != '\0') {
                std::error_code ec;
                if (fs::is_regular_file(override_path, ec)) {
                    return override_path;
                }
            }
        }
        const char* path_env = std::getenv("PATH");
        if (path_env == nullptr) {
            return "tectonic";
        }
        std::string_view paths(path_env);
        constexpr std::string_view separator =
#ifdef _WIN32
            ";";
#else
            ":";
#endif
        std::size_t start = 0;
        while (start <= paths.size()) {
            const std::size_t end = paths.find(separator, start);
            const auto segment = paths.substr(start, end == std::string_view::npos
                                                         ? std::string_view::npos
                                                         : end - start);
            if (!segment.empty()) {
                fs::path candidate(segment);
                candidate /= "tectonic"
#ifdef _WIN32
                             ".exe"
#endif
                    ;
                std::error_code ec;
                if (fs::is_regular_file(candidate, ec)) {
                    return candidate.generic_string();
                }
            }
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }
        return "tectonic";
    }

    static core::contracts::CompileResult finish(core::contracts::ICompileProgress* progress,
                                                 core::contracts::CompileResult result) {
        if (progress != nullptr) {
            progress->onFinished(result);
        }
        return result;
    }

    static std::optional<std::filesystem::path> find_output_pdf(
        const std::filesystem::path& project_root, const std::filesystem::path& main_file) {
        namespace fs = std::filesystem;

        std::vector<fs::path> candidates{
            project_root / main_file,
            project_root / "build" / main_file,
            project_root / "out" / main_file,
            project_root / "target" / main_file,
        };
        for (const auto& candidate : candidates) {
            auto pdf = candidate;
            pdf.replace_extension(".pdf");
            std::error_code ec;
            if (fs::is_regular_file(pdf, ec)) {
                return pdf;
            }
        }

        std::error_code ec;
        fs::recursive_directory_iterator it(project_root, fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        std::optional<fs::path> newest;
        auto newest_time = fs::file_time_type::min();
        while (!ec && it != end) {
            if (it->is_regular_file(ec) && it->path().extension() == ".pdf") {
                const auto modified = it->last_write_time(ec);
                if (!ec && modified > newest_time) {
                    newest_time = modified;
                    newest = it->path();
                }
            }
            it.increment(ec);
        }
        return newest;
    }

    std::shared_ptr<core::contracts::IProcessRunner> runner_;
};

} // namespace tectonic
} // namespace adapters

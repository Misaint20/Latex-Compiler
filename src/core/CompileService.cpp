#include "CompileService.hpp"
#include "DiagramBuildService.hpp"
#include "MultiEngineCompiler.hpp"
#include "contracts/ICompileProgress.hpp"
#include "contracts/IFileOpener.hpp"
#include "contracts/INotifier.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace core
{
    namespace
    {

        const char *state_name(CompileService::JobState state)
        {
            switch (state)
            {
            case CompileService::JobState::Idle:
                return "idle";
            case CompileService::JobState::Running:
                return "running";
            case CompileService::JobState::Succeeded:
                return "succeeded";
            case CompileService::JobState::Failed:
                return "failed";
            case CompileService::JobState::Canceled:
                return "canceled";
            }
            return "idle";
        }

        // Escapes a raw UTF-8 string for safe embedding inside a JSON payload.
        std::string json_escape(const std::string &raw)
        {
            std::string out;
            out.reserve(raw.size() + 8);
            for (const unsigned char c : raw)
            {
                switch (c)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\b':
                    out += "\\b";
                    break;
                case '\f':
                    out += "\\f";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (c < 0x20)
                    {
                        char buffer[7];
                        std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                        out += buffer;
                    }
                    else
                    {
                        out += static_cast<char>(c);
                    }
                }
            }
            return out;
        }

    } // namespace

    class CompileService::JobProgress : public contracts::ICompileProgress
    {
    public:
        explicit JobProgress(CompileService &service) : service_(service) {}

        void onOutput(const std::string &chunk) override
        {
            std::lock_guard<std::mutex> lock(service_.job_mutex_);
            service_.pushLogLocked(chunk, true);
        }

        void onProgress(const contracts::ProgressInfo &info) override
        {
            std::lock_guard<std::mutex> lock(service_.job_mutex_);
            service_.handleProgressLocked(info);
        }

        void onFinished(const contracts::CompileResult &result) override
        {
            service_.finishJob(result);
        }

    private:
        CompileService &service_;
    };

    CompileService::CompileService(
        std::shared_ptr<contracts::ICompiler> compiler,
        std::shared_ptr<contracts::IFileStore> fileStore,
        std::shared_ptr<contracts::IEventPublisher> eventPublisher,
        std::shared_ptr<contracts::IFileOpener> fileOpener,
        std::shared_ptr<contracts::ICompileHistory> history)
        : compiler_(std::move(compiler)),
          fileStore_(std::move(fileStore)),
          eventPublisher_(std::move(eventPublisher)),
          fileOpener_(std::move(fileOpener)),
          history_(std::move(history))
    {
    }

    CompileService::~CompileService()
    {
        if (worker_.joinable())
        {
            cancelActive();
            worker_.join();
        }
    }

    contracts::CompileResult CompileService::compileProject(const contracts::CompileRequest &request)
    {
        eventPublisher_->publish({"compile_started", R"({"project": ")" + request.projectPath + R"("})"});

        const auto started_at = std::chrono::steady_clock::now();
        buildDiagramsForJob(0, 5, request.projectPath, true);
        auto result = compiler_->compile(request);
        const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at)
                                     .count();

        if (result.success)
        {
            eventPublisher_->publish({"compile_success", R"({"output": ")" + result.outputPath + R"("})"});
        }
        else
        {
            eventPublisher_->publish({"compile_error", R"({"error": ")" + result.errorMessage + R"("})"});
        }

        if (history_)
        {
            contracts::HistoryEntry entry;
            entry.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();
            entry.duration_ms = duration_ms;
            entry.result = result.success ? "succeeded" : "failed";
            entry.main_file = request.mainFile;
            entry.output_path = result.outputPath;
            entry.error_message = result.errorMessage;
            history_->record(request.projectPath, entry);
        }

        return result;
    }

    bool CompileService::startCompile(const contracts::CompileRequest &request)
    {
        {
            std::lock_guard<std::mutex> lock(job_mutex_);
            if (state_ == JobState::Running)
            {
                return false;
            }
            if (worker_.joinable())
            {
                worker_.join();
            }
            state_ = JobState::Running;
            progress_percent_ = 0;
            message_ = "Starting compilation...";
            stage_.clear();
            stage_key_.clear();
            current_file_.clear();
            stages_reached_.clear();
            failed_stage_key_.clear();
            stage_durations_ms_.clear();
            open_stage_key_.clear();
            output_path_.clear();
            log_lines_.clear();
            pending_output_.clear();
            notice_.clear();
            active_request_ = request;
            started_at_ = std::chrono::steady_clock::now();
            start_recorded_ = true;
            // Only the async (user-facing) path may ring the OS; the sync
            // API is the in-process path where a notification is noise.
            notify_on_finish_ = notifier_ != nullptr;
        }

        publish("compile_started", R"({"project": ")" + request.projectPath + R"("})");

        worker_ = std::thread([this, request]()
                              {
        setProgressLocked(5, "Compiling...");
        buildDiagramsForJob(5, 15, request.projectPath, true);
        JobProgress progress(*this);
        auto result = compiler_->compile(request, &progress);
        // onFinished normally completes the job first; this covers compilers
        // that skip the progress callback.
        finishJob(std::move(result)); });
        return true;
    }

    CompileService::JobStatus CompileService::jobStatus() const
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        JobStatus status;
        status.state = state_;
        status.progress_percent = progress_percent_;
        status.message = message_;
        status.stage = stage_;
        status.stage_key = stage_key_;
        status.current_file = current_file_;
        status.stages_reached = stages_reached_;
        status.failed_stage_key = failed_stage_key_;
        status.stage_durations_ms = stage_durations_ms_;
        if (!open_stage_key_.empty())
        {
            status.stage_durations_ms[open_stage_key_] +=
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - stage_started_at_)
                    .count();
        }
        status.output_path = output_path_;
        status.log_tail = logTailLocked();
        status.notice = notice_;
        return status;
    }

    bool CompileService::cancelActive()
    {
        {
            std::lock_guard<std::mutex> lock(job_mutex_);
            if (state_ != JobState::Running)
            {
                return false;
            }
            setProgressLocked(progress_percent_, "Canceling...");
        }
        compiler_->cancel("active");
        return true;
    }

    bool CompileService::openLastOutput()
    {
        std::string output_path;
        {
            std::lock_guard<std::mutex> lock(job_mutex_);
            output_path = output_path_;
        }
        if (output_path.empty() || !fileOpener_)
        {
            return false;
        }
        return fileOpener_->open(output_path);
    }

    std::vector<contracts::HistoryEntry> CompileService::history(const std::string &projectPath)
    {
        if (!history_)
        {
            return {};
        }
        return history_->entries(projectPath);
    }

    std::vector<contracts::EngineInfo> CompileService::availableEngines() const
    {
        return compiler_->availableEngines();
    }

    std::string CompileService::preferredEngine() const
    {
        auto *selector = dynamic_cast<const core::MultiEngineCompiler *>(compiler_.get());
        return selector ? selector->preferredEngine() : std::string{"auto"};
    }

    void CompileService::setPreferredEngine(const std::string &engine_id)
    {
        auto *selector = dynamic_cast<core::MultiEngineCompiler *>(compiler_.get());
        if (selector)
        {
            selector->setPreferredEngine(engine_id);
        }
    }

    bool CompileService::isKnownEngine(const std::string &engine_id) const
    {
        auto *selector = dynamic_cast<core::MultiEngineCompiler *>(compiler_.get());
        if (selector)
        {
            return selector->isKnownEngine(engine_id);
        }
        return false;
    }

    void CompileService::setProgressLocked(int percent, std::string message)
    {
        progress_percent_ = std::clamp(percent, 0, 100);
        message_ = std::move(message);
    }

    void CompileService::handleProgressLocked(const contracts::ProgressInfo &info)
    {
        progress_percent_ = std::clamp(std::max(progress_percent_, info.percent), 0, 100);
        if (!info.stage.empty())
        {
            if (info.stage_key != stage_key_)
            {
                closeStageLocked();
            }
            stage_ = info.stage;
            stage_key_ = info.stage_key;
            if (open_stage_key_ != info.stage_key)
            {
                open_stage_key_ = info.stage_key;
                stage_started_at_ = std::chrono::steady_clock::now();
            }
        }
        if (!info.file.empty())
        {
            current_file_ = info.file;
        }
        if (!info.stage_key.empty() &&
            std::find(stages_reached_.begin(), stages_reached_.end(), info.stage_key) ==
                stages_reached_.end())
        {
            stages_reached_.push_back(info.stage_key);
        }
        if (!eventPublisher_)
        {
            return;
        }
        // The UI listens on the push channel; the reached-keys array drives the
        // named stage chips without an extra status round-trip.
        std::string keys;
        for (const auto &key : stages_reached_)
        {
            if (!keys.empty())
            {
                keys += ",";
            }
            keys += "\"" + json_escape(key) + "\"";
        }
        publish("compile_progress",
                "{\"progress\": " + std::to_string(progress_percent_) +
                    ", \"stage\": \"" + json_escape(stage_) +
                    "\", \"stageKey\": \"" + json_escape(stage_key_) +
                    "\", \"stagesReached\": [" + keys +
                    "], \"file\": \"" + json_escape(current_file_) + "\"}");
    }

    void CompileService::closeStageLocked()
    {
        if (open_stage_key_.empty())
        {
            return;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - stage_started_at_)
                                 .count();
        stage_durations_ms_[open_stage_key_] += elapsed;
        open_stage_key_.clear();
    }

    void CompileService::pushLogLocked(const std::string &chunk, bool emit_events)
    {
        pending_output_ += chunk;

        // Emit complete lines as events; keep the trailing partial line buffered.
        std::string::size_type newline = pending_output_.find('\n');
        while (newline != std::string::npos)
        {
            const std::string line = pending_output_.substr(0, newline);
            pending_output_.erase(0, newline + 1);

            log_lines_.push_back(line);
            while (log_lines_.size() > kMaxLogLines)
            {
                log_lines_.pop_front();
            }
            if (emit_events)
            {
                publish("compile_output", "{\"line\": \"" + json_escape(line) + "\"}");
            }
            newline = pending_output_.find('\n');
        }

        // Surface long partial lines too, so silent phases still show activity.
        if (emit_events && pending_output_.size() > 512)
        {
            publish("compile_output", "{\"partial\": \"" + json_escape(pending_output_) + "\"}");
            pending_output_.clear();
        }
    }

    std::string CompileService::logTailLocked() const
    {
        std::string tail;
        const std::size_t max_chars = 8000;
        for (const auto &line : log_lines_)
        {
            if (tail.size() + line.size() + 1 > max_chars)
            {
                continue;
            }
            tail += line;
            tail += '\n';
        }
        return tail;
    }

    void CompileService::setDiagramBuilder(std::shared_ptr<DiagramBuildService> builder)
    {
        diagram_builder_ = std::move(builder);
    }

    void CompileService::setNotifier(std::shared_ptr<contracts::INotifier> notifier)
    {
        notifier_ = std::move(notifier);
    }

    void CompileService::buildDiagramsForJob(std::int64_t base_percent,
                                             std::int64_t end_percent,
                                             const std::string &project_path,
                                             bool emit_events)
    {
        if (!diagram_builder_)
        {
            return;
        }
        setProgressLocked(static_cast<int>(base_percent), "Building diagrams...");
        handleProgressLocked({0, "diagrams", "diagrams", ""});
        const auto summary = diagram_builder_->buildAll(project_path);
        {
            std::lock_guard<std::mutex> lock(job_mutex_);
            if (!summary.toolchain_error.empty())
            {
                notice_ = notice_.empty() ? "Diagram toolchain not found (" + summary.toolchain_error +
                                                "). " + std::to_string(summary.rendered) + " rendered."
                                          : notice_ + " | Diagram toolchain not found (" + summary.toolchain_error + ").";
                pushLogLocked("diagram build: skipped, " + summary.toolchain_error +
                                  " (0 rendered; diagrams may be stale)\n",
                              emit_events);
            }
            else if (summary.failed > 0)
            {
                const std::string detail =
                    summary.error.empty() ? std::to_string(summary.failed) + " diagram(s) failed" : summary.error;
                notice_ = notice_.empty() ? "Diagram build warning: " + detail : notice_ + " | " + detail;
                pushLogLocked("diagram build: " + detail + "\n", emit_events);
            }
            else
            {
                std::string line = "diagram build: " + std::to_string(summary.rendered) + " rendered";
                if (summary.skipped > 0)
                {
                    line += ", " + std::to_string(summary.skipped) + " up to date";
                }
                if (summary.full_rebuild)
                {
                    line += " (toolchain changed: full rebuild)";
                }
                pushLogLocked(line + "\n", emit_events);
            }
        }
        setProgressLocked(static_cast<int>(end_percent), "Compiling...");
    }

    void CompileService::finishJob(contracts::CompileResult result)
    {
        JobState final_state = JobState::Idle;
        std::int64_t duration_ms = 0;
        contracts::CompileRequest request;
        contracts::HistoryEntry entry;
        {
            std::lock_guard<std::mutex> lock(job_mutex_);
            if (state_ != JobState::Running)
            {
                return;
            }
            duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started_at_)
                              .count();
            request = active_request_;
            closeStageLocked();

            if (result.success)
            {
                state_ = JobState::Succeeded;
                progress_percent_ = 100;
                message_ = "Compilation finished.";
                output_path_ = result.outputPath;
                publish("compile_success", R"({"output": ")" + result.outputPath + R"("})");
            }
            else if (result.errorMessage == "Compilation canceled.")
            {
                state_ = JobState::Canceled;
                message_ = "Compilation canceled.";
                publish("compile_canceled", "{}");
            }
            else
            {
                state_ = JobState::Failed;
                progress_percent_ = 100;
                message_ = result.errorMessage.empty() ? "Compilation failed." : result.errorMessage;
                failed_stage_key_ = stage_key_;
                publish("compile_error", "{\"error\": \"" + json_escape(message_) +
                                             "\", \"stageKey\": \"" + json_escape(stage_key_) + "\"}");
            }
            if (!result.notice.empty())
            {
                // Accumulate: an engine fallback notice must not erase the
                // diagram warnings gathered earlier in the same job.
                notice_ = notice_.empty() ? result.notice : notice_ + " | " + result.notice;
            }
            final_state = state_;
            entry.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();
            entry.duration_ms = duration_ms;
            entry.result = state_name(final_state);
            entry.main_file = request.mainFile;
            entry.output_path = result.outputPath;
            entry.error_message = result.errorMessage;
            entry.stages_reached = stages_reached_;
            entry.stage_durations_ms = stage_durations_ms_;
            entry.notice = notice_;
        }

        if (notify_on_finish_ && notifications_enabled_.load() && notifier_ &&
            (final_state == JobState::Succeeded || final_state == JobState::Failed))
        {
            std::string body;
            if (final_state == JobState::Succeeded)
            {
                const std::size_t slash = result.outputPath.find_last_of("/\\");
                body = slash == std::string::npos ? result.outputPath
                                                  : result.outputPath.substr(slash + 1);
                if (!notice_.empty())
                {
                    if (!body.empty())
                    {
                        body += " — ";
                    }
                    body += notice_.substr(0, 160);
                }
            }
            else
            {
                body = message_.substr(0, 160);
            }
            // Clicking the notification fronts the app and opens the PDF;
            // a failure has nothing to open.
            notifier_->notify(final_state == JobState::Succeeded ? "Compile finished" : "Compile failed",
                              body, final_state == JobState::Succeeded ? result.outputPath : "");
        }

        if (!history_)
        {
            return;
        }
        history_->record(request.projectPath, entry);
    }

    void CompileService::publish(const char *topic, const std::string &payload)
    {
        if (eventPublisher_)
        {
            eventPublisher_->publish({topic, payload});
        }
    }

} // namespace core

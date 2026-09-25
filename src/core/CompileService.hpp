#pragma once

#include "contracts/ICompileHistory.hpp"
#include "contracts/ICompiler.hpp"
#include "contracts/IEventPublisher.hpp"
#include "contracts/IFileStore.hpp"
#include <cstdint>

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace core {

class DiagramBuildService;
namespace contracts {
class IFileOpener;
class INotifier;
}

class CompileService {
public:
    static constexpr std::size_t kMaxLogLines = 400;

    enum class JobState {
        Idle,
        Running,
        Succeeded,
        Failed,
        Canceled,
    };

    struct JobStatus {
        JobState state = JobState::Idle;
        int progress_percent = 0;
        std::string message;
        std::string stage;
        std::string stage_key;
        std::string current_file;
        std::vector<std::string> stages_reached;
        std::string failed_stage_key;
        std::map<std::string, std::int64_t> stage_durations_ms;
        std::string output_path;
        std::string log_tail;
        // Non-fatal engine notice (e.g. auto-selection fallback).
        std::string notice;
    };

    CompileService(
        std::shared_ptr<contracts::ICompiler> compiler,
        std::shared_ptr<contracts::IFileStore> fileStore,
        std::shared_ptr<contracts::IEventPublisher> eventPublisher,
        std::shared_ptr<contracts::IFileOpener> fileOpener = nullptr,
        std::shared_ptr<contracts::ICompileHistory> history = nullptr);
    ~CompileService();

    // OS notifications fire only for the async API (the user-facing path);
    // the synchronous API exists for tests and callers that stay blocked.
    void setNotifier(std::shared_ptr<contracts::INotifier> notifier);

    // User preference gate for OS notifications; checked at job-finish time
    // so toggling mid-compile applies to the run in flight.
    void setNotificationsEnabled(bool enabled) { notifications_enabled_.store(enabled); }
    bool notificationsEnabled() const { return notifications_enabled_.load(); }

    // Synchronous API kept for tests and simple callers.
    contracts::CompileResult compileProject(const contracts::CompileRequest& request);

    // Async API: spawns a worker thread, streams progress and output chunks.
    bool startCompile(const contracts::CompileRequest& request);
    JobStatus jobStatus() const;
    bool cancelActive();

    // Opens the last successful job's output with the OS default handler.
    // Returns false when there is no output yet or the opener is missing.
    bool openLastOutput();

    // Engine catalogue for the settings UI, and the persisted preference.
    std::vector<contracts::EngineInfo> availableEngines() const;
    std::string preferredEngine() const;
    void setPreferredEngine(const std::string& engine_id);
    bool isKnownEngine(const std::string& engine_id) const;

    // Full history for a project, newest first.
    std::vector<contracts::HistoryEntry> history(const std::string& projectPath);

    // Builds the SVG/PDF/PNG artifacts the document demands from its .mmd
    // sources. Called before each compile when a builder is installed.
    void setDiagramBuilder(std::shared_ptr<DiagramBuildService> builder);

private:
    class JobProgress;
    void setProgressLocked(int percent, std::string message);
    void handleProgressLocked(const contracts::ProgressInfo& info);
    void closeStageLocked();
    void pushLogLocked(const std::string& chunk, bool emit_events);
    std::string logTailLocked() const;
    void finishJob(contracts::CompileResult result);
    void publish(const char* topic, const std::string& payload);
    void buildDiagramsForJob(std::int64_t base_percent, std::int64_t end_percent,
                             const std::string& project_path, bool emit_events);

    std::shared_ptr<contracts::ICompiler> compiler_;
    std::shared_ptr<contracts::IFileStore> fileStore_;
    std::shared_ptr<contracts::IEventPublisher> eventPublisher_;
    std::shared_ptr<contracts::IFileOpener> fileOpener_;
    std::shared_ptr<contracts::ICompileHistory> history_;
    std::shared_ptr<DiagramBuildService> diagram_builder_;
    std::shared_ptr<contracts::INotifier> notifier_;
    // Set only when the service serves the async API (see constructor). The
    // sync API is the in-process path, where an OS notification is noise.
    bool notify_on_finish_ = false;
    // User preference (settings UI); default on.
    std::atomic<bool> notifications_enabled_{true};

    mutable std::mutex job_mutex_;
    JobState state_ = JobState::Idle;
    int progress_percent_ = 0;
    std::string message_;
    std::string stage_;
    std::string stage_key_;
    std::string current_file_;
    std::vector<std::string> stages_reached_;
    std::string failed_stage_key_;
    // Stage timing: a stage runs from its first note: line until the next
    // stage's first line; the last one closes when the job ends.
    std::map<std::string, std::int64_t> stage_durations_ms_;
    std::string open_stage_key_;
    std::chrono::steady_clock::time_point stage_started_at_;
    std::string output_path_;
    std::deque<std::string> log_lines_;
    std::string pending_output_;
    std::string notice_;

    // Job bookkeeping captured at start so finishJob can record history.
    contracts::CompileRequest active_request_;
    std::chrono::steady_clock::time_point started_at_;
    bool start_recorded_ = false;

    std::thread worker_;
};

} // namespace core

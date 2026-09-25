#pragma once

#include "core/contracts/ICompileHistory.hpp"
#include "core/contracts/IFileStore.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace adapters {
namespace history {

// Persists one JSON file per project under a cache directory, storing the last
// N compile runs (timestamp, duration, result, main file, output, error).
class JsonCompileHistory : public core::contracts::ICompileHistory {
public:
    static constexpr std::size_t kMaxEntriesPerProject = 50;

    explicit JsonCompileHistory(std::shared_ptr<core::contracts::IFileStore> fileStore,
                                std::string storageDir)
        : fileStore_(std::move(fileStore)), storageDir_(std::move(storageDir)) {
        std::error_code ec;
        std::filesystem::create_directories(storageDir_, ec);
    }

    std::vector<core::contracts::HistoryEntry>
    entries(const std::string& projectPath) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto result = entries_unlocked(projectPath);
        std::sort(result.begin(), result.end(),
                  [](const auto& a, const auto& b) { return a.timestamp_ms > b.timestamp_ms; });
        return result;
    }

    void record(const std::string& projectPath,
                const core::contracts::HistoryEntry& entry) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto existing = entries_unlocked(projectPath);
        existing.push_back(entry);
        std::sort(existing.begin(), existing.end(),
                  [](const auto& a, const auto& b) { return a.timestamp_ms > b.timestamp_ms; });
        if (existing.size() > kMaxEntriesPerProject) {
            existing.resize(kMaxEntriesPerProject);
        }

        auto document = nlohmann::json::array();
        for (const auto& item : existing) {
            nlohmann::json stages = nlohmann::json::array();
            for (const auto& stage : item.stages_reached) {
                stages.push_back(stage);
            }
            nlohmann::json durations = nlohmann::json::object();
            for (const auto& [key, ms] : item.stage_durations_ms) {
                durations[key] = ms;
            }
            document.push_back({
                {"timestampMs", item.timestamp_ms},
                {"durationMs", item.duration_ms},
                {"result", item.result},
                {"mainFile", item.main_file},
                {"outputPath", item.output_path},
                {"errorMessage", item.error_message},
                {"stagesReached", stages},
                {"stageDurationsMs", durations},
            });
        }
        try {
            fileStore_->writeFile(file_path_for(projectPath), document.dump(2));
        } catch (const std::exception&) {
            // Losing one history write must never break a compile flow.
        }
    }

private:
    std::vector<core::contracts::HistoryEntry> entries_unlocked(const std::string& projectPath) {
        std::vector<core::contracts::HistoryEntry> result;
        const std::string path = file_path_for(projectPath);
        if (!fileStore_->exists(path)) {
            return result;
        }
        try {
            const auto document = nlohmann::json::parse(fileStore_->readFile(path));
            for (const auto& item : document) {
                core::contracts::HistoryEntry entry;
                entry.timestamp_ms = item.value("timestampMs", std::int64_t{0});
                entry.duration_ms = item.value("durationMs", std::int64_t{0});
                entry.result = item.value("result", std::string{});
                entry.main_file = item.value("mainFile", std::string{});
                entry.output_path = item.value("outputPath", std::string{});
                entry.error_message = item.value("errorMessage", std::string{});
                if (item.contains("stagesReached")) {
                    for (const auto& stage : item["stagesReached"]) {
                        entry.stages_reached.push_back(stage.get<std::string>());
                    }
                    // Named local: iterating a temporary json's items() keeps
                    // iterators into destroyed stack memory.
                    const auto durations = item.value("stageDurationsMs", nlohmann::json::object());
                    for (const auto& [key, ms] : durations.items()) {
                        entry.stage_durations_ms[key] = ms.get<std::int64_t>();
                    }
                }
                result.push_back(std::move(entry));
            }
        } catch (const std::exception&) {
            return {};
        }
        return result;
    }

    std::string file_path_for(const std::string& projectPath) const {
        std::string digest = projectPath;
        // Sanitize the project path into a single safe file name component.
        std::string safe;
        safe.reserve(digest.size());
        for (const unsigned char c : digest) {
            if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.') {
                safe += static_cast<char>(c);
            } else {
                safe += '_';
            }
        }
        if (safe.size() > 120) {
            safe = safe.substr(safe.size() - 120);
        }
        return (std::filesystem::path(storageDir_) / ("history-" + safe + ".json")).generic_string();
    }

    std::shared_ptr<core::contracts::IFileStore> fileStore_;
    std::string storageDir_;
    std::mutex mutex_;
};

} // namespace history
} // namespace adapters

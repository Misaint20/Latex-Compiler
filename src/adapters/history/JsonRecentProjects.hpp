#pragma once

#include "core/contracts/IFileStore.hpp"
#include "core/contracts/IRecentProjects.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace adapters {
namespace history {

// Persists the recent-project list as a single JSON file written through the
// IFileStore contract. Recording is MRU ordered and capped; the head of the
// list is what the app reopens on launch.
//
// Existence checks against the filesystem are cached for a short TTL so the
// home screen renders instantly; once the TTL lapses the next list() pays for
// one probe round and refreshes the cache. A clock that moves backwards
// (NTP correction, timezone change) distrusts the cached probe.
class JsonRecentProjects : public core::contracts::IRecentProjects {
public:
    static constexpr std::size_t kMaxEntries = 10;
    static constexpr std::chrono::seconds kExistsTtl{60};

    JsonRecentProjects(std::shared_ptr<core::contracts::IFileStore> fileStore,
                       std::string storageDir,
                       std::chrono::seconds exists_ttl = kExistsTtl,
                       std::function<std::int64_t()> clock = {})
        : fileStore_(std::move(fileStore)),
          storage_dir_(std::move(storageDir)),
          exists_ttl_(exists_ttl),
          ttl_ms_(std::chrono::duration_cast<std::chrono::milliseconds>(exists_ttl).count()),
          clock_(clock ? std::move(clock) : [] {
              return std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                  .count();
          }) {
        std::error_code ec;
        std::filesystem::create_directories(storage_dir_, ec);
    }

    std::vector<core::contracts::RecentProject> list() override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto entries = list_unlocked();
        apply_existence_unlocked(entries);
        return entries;
    }

    std::size_t removeMissing() override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto entries = list_unlocked();
        apply_existence_unlocked(entries);
        return purge_unlocked(std::move(entries));
    }

    std::size_t removeMissingForceProbe() override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto entries = list_unlocked();
        probe_all_unlocked(entries);
        existence_probe_ms_ = clock_();

        std::unordered_map<std::string, bool> verdicts;
        verdicts.reserve(entries.size());
        for (const auto& entry : entries) {
            verdicts.emplace(entry.path, entry.exists);
        }
        existence_exists_ = std::move(verdicts);
        return purge_unlocked(std::move(entries));
    }

    void rememberMainFile(const std::string& path, const std::string& main_file) override {
        if (path.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto entries = list_unlocked();
        std::string canonical = path;
        std::error_code ec;
        const auto resolved = std::filesystem::weakly_canonical(path, ec);
        if (!ec) {
            canonical = resolved.generic_string();
        }
        bool found = false;
        for (auto& entry : entries) {
            if (entry.path == canonical) {
                entry.main_file = main_file;
                found = true;
                break;
            }
        }
        if (!found) {
            entries.insert(entries.begin(), {canonical, static_cast<std::int64_t>(0), true, main_file, std::string{}});
            // A brand-new path has no cached existence verdict yet.
            existence_exists_.erase(canonical);
        }
        save_unlocked(entries);
    }

    void record(const std::string& path) override {
        if (path.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto entries = list_unlocked();
        const auto now = clock_();
        std::string canonical = path;
        std::error_code ec;
        const auto resolved = std::filesystem::weakly_canonical(path, ec);
        if (!ec) {
            canonical = resolved.generic_string();
        }

        // Preserve the remembered main-file choice and last tab across
        // re-opens.
        std::string remembered_main_file;
        std::string remembered_tab;
        for (auto& entry : entries) {
            if (entry.path == canonical) {
                remembered_main_file = entry.main_file;
                remembered_tab = entry.last_tab;
                break;
            }
        }

        std::vector<core::contracts::RecentProject> next;
        next.push_back({canonical, now, true, remembered_main_file, remembered_tab});
        for (auto& entry : entries) {
            if (entry.path != canonical) {
                next.push_back(std::move(entry));
            }
        }
        if (next.size() > kMaxEntries) {
            next.resize(kMaxEntries);
        }
        save_unlocked(next);
    }

    void rememberLastTab(const std::string& path, const std::string& tab) override {
        if (path.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto entries = list_unlocked();
        std::string canonical = path;
        std::error_code ec;
        const auto resolved = std::filesystem::weakly_canonical(path, ec);
        if (!ec) {
            canonical = resolved.generic_string();
        }
        // Only existing entries are updated: switching a tab must not create
        // a recents entry nor reorder the MRU list.
        for (auto& entry : entries) {
            if (entry.path == canonical) {
                entry.last_tab = tab;
                save_unlocked(entries);
                return;
            }
        }
    }

    bool remove(const std::string& path) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto entries = list_unlocked();
        std::string canonical = path;
        std::error_code ec;
        const auto resolved = std::filesystem::weakly_canonical(path, ec);
        if (!ec) {
            canonical = resolved.generic_string();
        }
        const auto before = entries.size();
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&](const auto& entry) { return entry.path == canonical; }),
                      entries.end());
        if (entries.size() == before) {
            return false;
        }
        save_unlocked(entries);
        return true;
    }

private:
    std::string file_path() const {
        return (std::filesystem::path(storage_dir_) / "recent-projects.json").generic_string();
    }

    std::vector<core::contracts::RecentProject> list_unlocked() {
        std::vector<core::contracts::RecentProject> entries;
        if (!fileStore_->exists(file_path())) {
            return entries;
        }
        try {
            const auto document = nlohmann::json::parse(fileStore_->readFile(file_path()));
            if (!document.is_array()) {
                return entries;
            }
            for (const auto& item : document) {
                core::contracts::RecentProject entry;
                entry.path = item.value("path", std::string{});
                entry.last_opened_ms = item.value("lastOpenedMs", std::int64_t{0});
                entry.main_file = item.value("mainFile", std::string{});
                entry.last_tab = item.value("lastTab", std::string{});
                if (!entry.path.empty()) {
                    entries.push_back(std::move(entry));
                }
            }
        } catch (const std::exception&) {
            return {};
        }
        return entries;
    }

    // Probes every entry directly against the filesystem, ignoring the cache.
    void probe_all_unlocked(std::vector<core::contracts::RecentProject>& entries) {
        for (auto& entry : entries) {
            std::error_code ec;
            entry.exists = std::filesystem::is_directory(entry.path, ec) && !ec;
        }
    }

    // Drops missing entries, persists when something changed; returns count.
    std::size_t purge_unlocked(std::vector<core::contracts::RecentProject> entries) {
        const auto before = entries.size();
        std::vector<core::contracts::RecentProject> kept;
        kept.reserve(entries.size());
        for (auto& entry : entries) {
            if (entry.exists) {
                kept.push_back(std::move(entry));
            }
        }
        const std::size_t removed = before - kept.size();
        if (removed > 0) {
            save_unlocked(kept);
        }
        return removed;
    }

    // Fills entry.exists from the TTL cache, probing the filesystem only for
    // paths without a cached verdict or once the TTL has lapsed. Cached
    // verdicts are keyed by path so a mutating list never mislabels entries.
    void apply_existence_unlocked(std::vector<core::contracts::RecentProject>& entries) {
        if (entries.empty()) {
            return;
        }
        const auto now = clock_();
        if (existence_probe_ms_ && *existence_probe_ms_ > now) {
            existence_probe_ms_.reset();
            existence_exists_.clear();
        }
        if (!existence_probe_ms_ || now - *existence_probe_ms_ >= ttl_ms_) {
            existence_probe_ms_ = now;
            existence_exists_.clear();
        }
        for (auto& entry : entries) {
            const auto cached = existence_exists_.find(entry.path);
            if (cached != existence_exists_.end()) {
                entry.exists = cached->second;
                continue;
            }
            std::error_code ec;
            entry.exists = std::filesystem::is_directory(entry.path, ec) && !ec;
            existence_exists_.emplace(entry.path, entry.exists);
        }
    }

    void save_unlocked(const std::vector<core::contracts::RecentProject>& entries) {
        auto document = nlohmann::json::array();
        for (const auto& entry : entries) {
            document.push_back({
                {"path", entry.path},
                {"lastOpenedMs", entry.last_opened_ms},
                {"mainFile", entry.main_file},
                {"lastTab", entry.last_tab},
            });
        }
        try {
            fileStore_->writeFile(file_path(), document.dump(2));
        } catch (const std::exception&) {
            // Losing one recents write must never break the app flow.
        }
    }

    std::shared_ptr<core::contracts::IFileStore> fileStore_;
    std::string storage_dir_;
    std::chrono::seconds exists_ttl_;
    std::int64_t ttl_ms_;
    std::function<std::int64_t()> clock_;
    std::mutex mutex_;

    // Existence verdicts from the last probe round (TTL-guarded, path-keyed).
    mutable std::optional<std::int64_t> existence_probe_ms_;
    std::unordered_map<std::string, bool> existence_exists_;
};

} // namespace history
} // namespace adapters

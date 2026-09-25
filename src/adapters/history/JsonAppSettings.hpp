#pragma once

#include "core/contracts/IAppSettings.hpp"
#include "core/contracts/IFileStore.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <mutex>
#include <string>

namespace adapters {
namespace history {

// Persists application settings as a single JSON file written through the
// IFileStore contract. The whole file is held in memory and rewritten on set.
class JsonAppSettings : public core::contracts::IAppSettings {
public:
    JsonAppSettings(std::shared_ptr<core::contracts::IFileStore> fileStore,
                    std::string storageDir)
        : fileStore_(std::move(fileStore)),
          storage_dir_(std::move(storageDir)) {
        std::error_code ec;
        std::filesystem::create_directories(storage_dir_, ec);
        load();
    }

    std::string get(const std::string& key, const std::string& fallback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = values_.find(key);
        return found == values_.end() ? fallback : found->second;
    }

    void set(const std::string& key, const std::string& value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        values_[key] = value;
        save();
    }

private:
    std::string file_path() const {
        return (std::filesystem::path(storage_dir_) / "settings.json").generic_string();
    }

    void load() {
        if (!fileStore_->exists(file_path())) {
            return;
        }
        try {
            const auto document = nlohmann::json::parse(fileStore_->readFile(file_path()));
            if (!document.is_object()) {
                return;
            }
            for (auto it = document.begin(); it != document.end(); ++it) {
                if (it.value().is_string()) {
                    values_[it.key()] = it.value().get<std::string>();
                }
            }
        } catch (const std::exception&) {
            // Corrupt settings fall back to defaults.
        }
    }

    void save() {
        auto document = nlohmann::json::object();
        for (const auto& [key, value] : values_) {
            document[key] = value;
        }
        try {
            fileStore_->writeFile(file_path(), document.dump(2));
        } catch (const std::exception&) {
            // Losing one settings write must never break the app flow.
        }
    }

    std::shared_ptr<core::contracts::IFileStore> fileStore_;
    std::string storage_dir_;
    std::map<std::string, std::string> values_;
    std::mutex mutex_;
};

} // namespace history
} // namespace adapters

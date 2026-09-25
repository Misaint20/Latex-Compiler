#pragma once

#include "core/contracts/IFileStore.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <string>

namespace adapters {
namespace filesystem {

class LocalFileStore : public core::contracts::IFileStore {
public:
    std::string readFile(const std::string& path) override {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Cannot read file: " + path);
        }
        std::ostringstream content;
        content << file.rdbuf();
        return content.str();
    }

    void writeFile(const std::string& path, const std::string& content) override {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("Cannot write file: " + path);
        }
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    bool exists(const std::string& path) const override {
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    }

    std::vector<std::string> listDirectory(const std::string& path) const override {
        std::vector<std::string> entries;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
            entries.push_back(entry.path().filename().generic_string());
        }
        return entries;
    }
};

} // namespace filesystem
} // namespace adapters

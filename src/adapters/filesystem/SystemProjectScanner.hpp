#pragma once

#include "core/contracts/IProjectScanner.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

namespace adapters {
namespace filesystem {

// Walks the project and groups every relevant file into chapters (.tex),
// diagrams (.mmd/.mermaid), style files (.sty/.cls/.bib/.cfg/.def), and assets,
// recording each file's subfolder.
class SystemProjectScanner : public core::contracts::IProjectScanner {
public:
    core::contracts::ProjectInfo scan(const std::string& directoryPath) override {
        namespace fs = std::filesystem;
        core::contracts::ProjectInfo info;
        info.projectPath = directoryPath;

        const fs::path root(directoryPath);
        std::error_code ec;
        if (directoryPath.empty() || !fs::is_directory(root, ec)) {
            return info;
        }

        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        while (!ec && it != end) {
            const auto& entry = *it;
            if (entry.is_directory(ec)) {
                const auto name = entry.path().filename().string();
                if (is_excluded_directory(name)) {
                    it.disable_recursion_pending();
                }
                it.increment(ec);
                continue;
            }
            if (!entry.is_regular_file(ec)) {
                it.increment(ec);
                continue;
            }

            const auto path = entry.path();
            const auto extension = to_lower(path.extension().string());
            const auto relative = std::filesystem::relative(path, root, ec);
            const auto folder = relative.empty() ? std::string{}
                                                 : relative.parent_path().generic_string();
            core::contracts::ProjectFile file{
                path.filename().generic_string(), folder, static_cast<std::uintmax_t>(entry.file_size(ec))};

            if (extension == ".tex") {
                info.texFiles.push_back(folder.empty() ? file.name : folder + "/" + file.name);
                if (is_main_candidate(path)) {
                    info.mainFileCandidate = folder.empty() ? file.name : folder + "/" + file.name;
                }
                info.chapters.push_back(std::move(file));
            } else if (extension == ".mmd" || extension == ".mermaid") {
                info.diagrams.push_back(std::move(file));
            } else if (is_style_extension(extension)) {
                info.styles.push_back(std::move(file));
            } else if (is_asset_extension(extension)) {
                info.assets.push_back(std::move(file));
            }

            it.increment(ec);
        }

        if (info.mainFileCandidate.empty() && !info.texFiles.empty()) {
            const auto shallowest = std::min_element(
                info.texFiles.begin(), info.texFiles.end(),
                [](const std::string& a, const std::string& b) {
                    return std::count(a.begin(), a.end(), '/') < std::count(b.begin(), b.end(), '/');
                });
            info.mainFileCandidate = *shallowest;
        }

        auto by_folder = [](const core::contracts::ProjectFile& a,
                            const core::contracts::ProjectFile& b) {
            if (a.folder != b.folder) {
                return a.folder < b.folder;
            }
            return a.name < b.name;
        };
        std::sort(info.chapters.begin(), info.chapters.end(), by_folder);
        std::sort(info.diagrams.begin(), info.diagrams.end(), by_folder);
        std::sort(info.styles.begin(), info.styles.end(), by_folder);
        std::sort(info.assets.begin(), info.assets.end(), by_folder);
        return info;
    }

private:
    static bool is_excluded_directory(const std::string& name) {
        if (name.empty()) {
            return false;
        }
        if (name.front() == '.') {
            return true;
        }
        static const char* k_excluded_names[] = {
            "node_modules", "bower_components", "vendor", "venv",
            "build", "dist", "out", "output", "target", "bin", "obj",
            "coverage", "__pycache__", "cache",
        };
        const auto lowered = to_lower(name);
        for (const auto* candidate : k_excluded_names) {
            if (lowered == candidate) {
                return true;
            }
        }
        return false;
    }

    static bool is_main_candidate(const std::filesystem::path& path) {
        const auto name = path.filename().string();
        if (name == "main.tex") {
            return true;
        }
        std::error_code ec;
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return false;
        }
        std::string line;
        while (std::getline(in, line)) {
            if (line.find("\\documentclass") != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    static std::string to_lower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    static bool is_style_extension(const std::string& extension) {
        return extension == ".sty" || extension == ".cls" || extension == ".bib" ||
               extension == ".cfg" || extension == ".def";
    }

    static bool is_asset_extension(const std::string& extension) {
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
               extension == ".gif" || extension == ".webp" || extension == ".bmp" ||
               extension == ".svg" || extension == ".pdf" ||
               extension == ".txt" || extension == ".md";
    }
};

} // namespace filesystem
} // namespace adapters

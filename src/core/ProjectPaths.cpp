#include "ProjectPaths.hpp"

#include <filesystem>
#include <utility>

namespace core {

std::optional<std::string> resolve_inside_project(const std::string& project_path,
                                                  const std::string& relative_path) {
    namespace fs = std::filesystem;

    if (project_path.empty() || relative_path.empty()) {
        return std::nullopt;
    }

    std::error_code ec;
    const fs::path root = fs::weakly_canonical(fs::path(project_path), ec);
    if (ec || root.empty()) {
        return std::nullopt;
    }

    fs::path target = fs::weakly_canonical(root / fs::path(relative_path), ec);
    if (ec) {
        return std::nullopt;
    }

    // Reject anything that escapes the project root via traversal or symlinks.
    const auto [root_it, target_it] = std::mismatch(
        root.begin(), root.end(), target.begin(), target.end());
    if (root_it != root.end()) {
        return std::nullopt;
    }

    return target.generic_string();
}

} // namespace core

#pragma once

#include <optional>
#include <string>

namespace core
{

    // Resolves a relative path inside a project root, rejecting traversal and
    // symlink escapes. Shared by services that touch project files.
    std::optional<std::string> resolve_inside_project(const std::string &project_path,
                                                      const std::string &relative_path);

} // namespace core

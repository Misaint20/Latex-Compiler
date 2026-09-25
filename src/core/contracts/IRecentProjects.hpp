#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cstddef>

namespace core {
namespace contracts {

struct RecentProject {
    std::string path;
    std::int64_t last_opened_ms = 0;
    // False when the folder no longer exists on disk.
    bool exists = true;
    // Main file the user picked for this project (relative path); empty when
    // the user has not overridden the automatic candidate.
    std::string main_file;
    // Last active explorer tab for this project ("chapters", "diagrams",
    // "styles", "assets", "history"); empty = default ("chapters").
    std::string last_tab;
};

class IRecentProjects {
public:
    virtual ~IRecentProjects() = default;

    // Most recent first; the first entry is the last opened project.
    virtual std::vector<RecentProject> list() = 0;
    // Moves the path to the front of the list (or appends it) and persists.
    virtual void record(const std::string& path) = 0;
    // Removes one path; returns false when it was not present.
    virtual bool remove(const std::string& path) = 0;
    // Drops every entry whose folder no longer exists (existence verdicts may
    // be TTL-cached); returns the count of removed entries.
    virtual std::size_t removeMissing() = 0;

    // Re-probes every entry against the filesystem, bypassing any existence
    // cache, and drops the missing ones. Returns the count of removed entries.
    virtual std::size_t removeMissingForceProbe() = 0;

    // Persists the user's main-file choice for a project (relative path).
    virtual void rememberMainFile(const std::string& path, const std::string& main_file) = 0;

    // Persists the last active explorer tab for a project. Ignored when the
    // project is not in the list: switching a tab must not create a recents
    // entry nor reorder the MRU list.
    virtual void rememberLastTab(const std::string& path, const std::string& tab) = 0;
};

} // namespace contracts
} // namespace core

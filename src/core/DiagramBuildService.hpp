#pragma once

#include "contracts/IProcessRunner.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace core {

// Materializes the SVG/PDF/PNG renderings of every .mmd source in the project
// before the LaTeX engine runs, so chapter files never fail on stale or
// missing diagram artifacts. The .mmd files are the single source of truth:
// the service reads each \includegraphics in the .tex sources, then rebuilds
// the requested format next to the .mmd whenever the source (or the shared
// theme/css) is newer than the target.
class DiagramBuildService {
public:
    struct Options {
        // Project-relative folders scanned recursively for .mmd sources.
        std::vector<std::string> source_dirs{"diagrams"};
        // Shared theme/css appended to every mmdc run when present in the
        // primary source dir (the convention this project's documents use).
        std::string theme_config = "mermaid-theme.json";
        std::string css = "mermaid.css";
        // Rewrites inline light-theme rgb() fills to dark brand tints before
        // rendering, matching the user's manual preprocessing script.
        bool color_remap = true;
        int timeout_seconds = 120;
    };

    struct Summary {
        int rendered = 0;
        int skipped = 0;
        int failed = 0;
        // First failure description, empty when everything succeeded.
        std::string error;
        // Why no tool ran when diagram sources exist but node/mmdc/
        // rsvg-convert could not be located; empty otherwise.
        std::string toolchain_error;
        // True when the resolved toolchain differs from the last run's stamp
        // (mermaid/rsvg upgrade, project moved): freshness was ignored and
        // every diagram was rebuilt once.
        bool full_rebuild = false;
    };

    explicit DiagramBuildService(std::shared_ptr<contracts::IProcessRunner> runner)
        : runner_(std::move(runner)) {}

    Summary buildAll(const std::string& project_path);

    void setOptions(Options options) { options_ = std::move(options); }

    // Extra directories probed before the defaults. With replace=true the
    // defaults (PATH, platform install dirs) are skipped entirely so tests
    // stay independent of the host toolchain.
    void setSearchPaths(std::vector<std::string> dirs, bool replace = false) {
        extra_search_paths_ = std::move(dirs);
        replace_default_paths_ = replace;
    }

private:
    struct ToolPaths {
        std::string node;
        std::string mmdc;
        std::string rsvg;
    };
    // Identity of the resolved toolchain: paths plus mtimes. Mermaid/rsvg
    // upgrades rewrite these files, so a change means every existing
    // artifact may predate the current renderer and must be rebuilt once.
    struct ToolchainIdentity {
        std::string mmdc_path;
        std::int64_t mmdc_mtime = 0;
        std::string rsvg_path;
        std::int64_t rsvg_mtime = 0;
    };

    // Target stem (lowercase, no ext) -> formats referenced by \includegraphics.
    std::map<std::string, std::set<std::string>> collectDemands(const std::filesystem::path& project) const;
    // Fills `error` with the missing tools when resolution fails.
    std::optional<ToolPaths> resolveTools(const std::filesystem::path& project, std::string& error) const;
    bool runTool(const std::string& command, const std::vector<std::string>& args,
                 const std::string& working_dir, std::string& log) const;
    // Points puppeteer at a system browser (config file) or installs the
    // official chrome-headless-shell once; returns whether a retry may run.
    bool recoverChrome(const std::filesystem::path& project, const ToolPaths& tools,
                       std::string& config_path, std::string& log) const;
    static bool fileNewerThan(const std::filesystem::path& file, const std::filesystem::path& reference);
    static std::string stripExtension(const std::string& file);
    ToolchainIdentity currentIdentity(const ToolPaths& tools) const;
    static std::string stampText(const ToolchainIdentity& identity);
    // False when the stamp is missing (first run) or differs from identity.
    static bool stampMatches(const std::filesystem::path& project, const ToolchainIdentity& identity);
    static void writeStamp(const std::filesystem::path& project, const ToolchainIdentity& identity);

    std::shared_ptr<contracts::IProcessRunner> runner_;
    Options options_;
    std::vector<std::string> extra_search_paths_;
    bool replace_default_paths_ = false;
};

} // namespace core

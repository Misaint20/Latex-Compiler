#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <cstdlib>
#else
#include <cstdlib>
#include <cstddef>
#include <utility>
#endif

namespace core {

// Locates executables across PATH plus the well-known install roots of TeX
// Live, MiKTeX, TinyTeX, Homebrew and Node, so a GUI process finds the
// toolchain even when launched with a minimal PATH.
class BinaryLocator {
public:
    static std::optional<std::string> find(const std::string& name,
                                           const std::vector<std::string>& extra_paths = {},
                                           bool replace_default_paths = false) {
        namespace fs = std::filesystem;
        const auto is_file = [](const fs::path& candidate) {
            std::error_code ec;
            return fs::is_regular_file(candidate, ec) && !fs::is_directory(candidate, ec);
        };
        for (const auto& directory : directories(extra_paths, replace_default_paths)) {
            fs::path candidate(directory);
            candidate /= name
#ifdef _WIN32
                + ".exe"
#endif
                ;
            if (is_file(candidate)) {
                return candidate.generic_string();
            }
        }
        return std::nullopt;
    }

private:
    static std::vector<std::string> directories(const std::vector<std::string>& extra_paths,
                                                bool replace_default_paths) {
        namespace fs = std::filesystem;
        if (replace_default_paths) {
            return dedupe(extra_paths);
        }
        std::vector<std::string> dirs = extra_paths;

        if (const char* path_env = std::getenv("PATH")) {
            std::string_view paths(path_env);
#ifdef _WIN32
            constexpr std::string_view separator = ";";
#else
            constexpr std::string_view separator = ":";
#endif
            std::size_t start = 0;
            while (start <= paths.size()) {
                const std::size_t end = paths.find(separator, start);
                const auto segment = paths.substr(start, end == std::string_view::npos
                                                             ? std::string_view::npos
                                                             : end - start);
                if (!segment.empty()) {
                    dirs.emplace_back(segment);
                }
                if (end == std::string_view::npos) {
                    break;
                }
                start = end + 1;
            }
        }

        // Node version managers (nvm/fnm/volta): GUI apps get a minimal PATH
        // that excludes them, so probe their well-known roots as well.
        if (const char* home = std::getenv("HOME")) {
            const fs::path home_dir(home);
            dirs.push_back((home_dir / ".volta" / "bin").generic_string());
            const fs::path version_roots[] = {
                home_dir / ".nvm" / "versions" / "node",
                home_dir / ".local" / "share" / "fnm" / "node-versions",
                home_dir / ".fnm" / "node-versions",
            };
            for (const auto& root : version_roots) {
                std::error_code ec;
                if (!fs::is_directory(root, ec)) {
                    continue;
                }
                for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
                     !ec && it != end; it.increment(ec)) {
                    if (!it->is_directory(ec)) {
                        continue;
                    }
#ifdef _WIN32
                    dirs.push_back(it->path().generic_string());
#else
                    dirs.push_back((it->path() / "bin").generic_string());
#endif
                }
                ec.clear();
            }
        }
#ifdef _WIN32
        if (const char* appdata = std::getenv("APPDATA")) {
            const fs::path nvm_windows = fs::path(appdata) / "nvm";
            std::error_code ec;
            for (fs::directory_iterator it(nvm_windows, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec)) {
                if (it->is_directory(ec)) {
                    dirs.push_back(it->path().generic_string());
                }
            }
            ec.clear();
        }
#endif

        // TeX Live/MiKTeX install roots: expand one level of version dirs.
        const auto expand = [&dirs](const fs::path& base, const std::string& bin_sub) {
            std::error_code ec;
            if (!fs::is_directory(base, ec)) {
                return;
            }
            dirs.push_back((base / bin_sub).generic_string());
            for (fs::directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec)) {
                if (it->is_directory(ec)) {
                    dirs.push_back((it->path() / bin_sub).generic_string());
                }
            }
        };
#ifdef __APPLE__
        dirs.push_back("/Library/TeX/texbin");
        dirs.push_back("/usr/local/bin");
        dirs.push_back("/opt/homebrew/bin");
        expand("/usr/local/texlive", "bin/universal-darwin");
        expand("/usr/local/texlive", "bin/x86_64-darwin");
        if (const char* home = std::getenv("HOME")) {
            const fs::path home_dir(home);
            dirs.push_back((home_dir / "Library" / "TinyTeX" / "bin" / "universal-darwin").generic_string());
            dirs.push_back((home_dir / "Library" / "TinyTeX" / "bin" / "x86_64-darwin").generic_string());
            dirs.push_back((home_dir / ".TinyTeX" / "bin" / "universal-darwin").generic_string());
        }
#elif defined(_WIN32)
        if (const char* local = std::getenv("LOCALAPPDATA")) {
            expand(fs::path(local) / "Programs" / "MiKTeX" / "miktex", "bin/x64");
        }
        expand("C:/texlive", "bin/windows");
        expand("C:/texlive", "bin/win32");
        if (const char* program_files = std::getenv("ProgramFiles")) {
            expand(fs::path(program_files) / "nodejs", "");
        }
#else
        dirs.push_back("/usr/local/bin");
        expand("/usr/local/texlive", "bin/x86_64-linux");
        expand("/opt/texlive", "bin/x86_64-linux");
        if (const char* home = std::getenv("HOME")) {
            const fs::path home_dir(home);
            dirs.push_back((home_dir / ".TinyTeX" / "bin" / "x86_64-linux").generic_string());
            dirs.push_back((home_dir / ".local" / "TinyTeX" / "bin" / "x86_64-linux").generic_string());
        }
#endif
        return dedupe(dirs);
    }

    static std::vector<std::string> dedupe(std::vector<std::string> dirs) {
        std::vector<std::string> unique;
        for (auto& dir : dirs) {
            while (dir.size() > 1 && dir.back() == '/') {
                dir.pop_back();
            }
            if (std::find(unique.begin(), unique.end(), dir) == unique.end()) {
                unique.push_back(std::move(dir));
            }
        }
        return unique;
    }
};

} // namespace core

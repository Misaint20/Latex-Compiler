#pragma once

#include "core/contracts/ICompiler.hpp"
#include "core/contracts/IProcessRunner.hpp"

#include <algorithm>
#include <cctype>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <cstddef>
#include <utility>

namespace adapters {
namespace classtex {

// Classic TeX Live toolchain (pdfLaTeX / XeLaTeX / LuaLaTeX) behind the same
// ICompiler contract as the Tectonic adapter. Runs the chosen engine with
// -interaction=nonstopmode, chains bibtex/biber when the document uses one,
// reruns until references stabilize and maps passes onto the canonical stage
// keys the UI renders.
class ClassicTexEngine : public core::contracts::ICompiler {
public:
    ClassicTexEngine(std::shared_ptr<core::contracts::IProcessRunner> runner)
        : runner_(std::move(runner)) {}

    // Extra directories probed before the defaults (tests, portable bundles).
    // With replace=true the defaults (PATH, platform install dirs) are skipped
    // entirely so tests stay independent of the host toolchain.
    void setSearchPaths(std::vector<std::string> dirs, bool replace = false) {
        extra_search_paths_ = std::move(dirs);
        replace_default_paths_ = replace;
    }

    std::vector<core::contracts::EngineInfo> availableEngines() override {
        std::vector<core::contracts::EngineInfo> engines;
        for (const auto& tool : tools()) {
            engines.push_back({tool.id, tool.title, locateBinary(tool.binary).has_value()});
        }
        return engines;
    }

    core::contracts::CompileResult compile(const core::contracts::CompileRequest& request) override {
        return compile(request, nullptr);
    }

    core::contracts::CompileResult compile(const core::contracts::CompileRequest& request,
                                           core::contracts::ICompileProgress* progress) override {
        namespace fs = std::filesystem;
        const fs::path project_root(request.projectPath);
        std::error_code ec;
        if (request.projectPath.empty() || !fs::is_directory(project_root, ec)) {
            return finish(progress, {false, {}, "Project path does not exist: " + request.projectPath, {}});
        }

        // Engine selection: an explicit id must exist; empty means auto —
        // documents using fontspec/unicode-math cannot run under pdfLaTeX,
        // so the canonical order puts XeLaTeX/LuaLaTeX first for them.
        const std::string main_name = fs::path(request.mainFile).filename().generic_string();
        const fs::path main_path = project_root / main_name;
        if (!fs::is_regular_file(main_path, ec)) {
            return finish(progress, {false, {}, "Main file does not exist: " + request.mainFile, {}});
        }
        const bool needs_unicode_engine = documentRequiresUnicodeEngine(main_path);
        std::string engine_id = request.engine;
        if (engine_id.empty()) {
            const auto order = needs_unicode_engine
                                   ? std::array<const char*, 3>{"xelatex", "lualatex", "pdflatex"}
                                   : std::array<const char*, 3>{"pdflatex", "xelatex", "lualatex"};
            for (const char* preferred : order) {
                if (locateBinary(preferred).has_value()) {
                    engine_id = preferred;
                    break;
                }
            }
            if (engine_id.empty()) {
                return finish(progress, {false, {},
                                         "No LaTeX engine found. Install TeX Live (pdflatex, xelatex or lualatex) and try again.",
                                         {}});
            }
        }
        const std::string binary = locateBinary(binaryFor(engine_id)).value_or("");
        if (binary.empty()) {
            return finish(progress, {false, {}, "Engine '" + engine_id + "' was not found on this system.", {}});
        }

        // The PDF next to the source is the pass artifact; a fresh one after
        // a failed exit code means the errors were recovered in nonstopmode.
        fs::path pdf_path = main_path;
        pdf_path.replace_extension(".pdf");
        std::error_code mtime_ec;
        const auto pdf_mtime_before = fs::last_write_time(pdf_path, mtime_ec);
        const bool pdf_existed_before = !mtime_ec && fs::is_regular_file(pdf_path, mtime_ec);
        auto freshPdf = [&]() {
            std::error_code fec;
            if (!fs::is_regular_file(pdf_path, fec) || fec) {
                return false;
            }
            if (!pdf_existed_before) {
                return true;
            }
            const auto modified = fs::last_write_time(pdf_path, fec);
            return !fec && modified > pdf_mtime_before;
        };

        std::string log;
        auto emit = [&](int percent, const char* key, const char* label) {
            if (progress == nullptr) {
                return;
            }
            core::contracts::ProgressInfo info;
            info.percent = percent;
            info.stage = label;
            info.stage_key = key;
            info.file = main_name;
            progress->onProgress(info);
        };

        // Decides the job outcome after a pass that exited nonzero: a fresh
        // PDF means recovered errors (success), otherwise surface the log.
        auto concludeAfterPass = [&](const std::string& pass_log) -> core::contracts::CompileResult {
            if (freshPdf()) {
                emit(100, "write", "Output");
                return finish(progress, {true, pdf_path.generic_string(), {}, {}});
            }
            return finish(progress, {false, {}, logTail(pass_log, 4000), {}});
        };

        // Pass 1: the engine echoes everything to stdout in nonstopmode.
        emit(8, "tex", "Pass 1");
        const std::vector<std::string> engine_args{
            "-interaction=nonstopmode", "-file-line-error", main_name};
        std::string pass_log;
        const auto first_pass = runPass(binary, project_root, engine_args, progress, pass_log);
        if (!first_pass.ok) {
            return concludeAfterPass(pass_log);
        }
        emit(40, "tex", "Pass 1");

        // Bibliography: detect from the main file text, run the matching tool
        // once; failures here are warnings, the rerun decides the outcome.
        const std::string bib_tool = bibliographyToolFor(main_path);
        const auto bib_binary = bib_tool.empty() ? std::nullopt : locateBinary(bib_tool);
        if (bib_tool.empty() == false && bib_binary.has_value()) {
            emit(45, "bibtex", "Bibliography");
            std::string bib_log;
            const std::string bib_base = fs::path(main_name).stem().generic_string();
            (void)runPass(*bib_binary, project_root, {bib_base}, progress, bib_log);
            log += bib_log;
        }

        // Reruns until the log stops asking for one (references stable).
        int percent = 55;
        for (int pass = 2; pass <= 4; ++pass) {
            emit(percent, "rerun", ("Pass " + std::to_string(pass)).c_str());
            pass_log.clear();
            const auto rerun_pass = runPass(binary, project_root, engine_args, progress, pass_log);
            if (!rerun_pass.ok) {
                return concludeAfterPass(pass_log);
            }
            emit(percent + 10, "rerun", ("Pass " + std::to_string(pass)).c_str());
            if (pass_log.find("Rerun to get") == std::string::npos &&
                pass_log.find("Label(s) may have changed") == std::string::npos) {
                break;
            }
            percent += 12;
        }

        // Classic engines write the PDF next to the source.
        if (!fs::is_regular_file(pdf_path, ec)) {
            return finish(progress, {false, {}, "Compilation finished but no PDF was produced.", {}});
        }
        emit(100, "write", "Output");
        return finish(progress, {true, pdf_path.generic_string(), {}, {}});
    }

    void cancel(core::contracts::CompileId) override {
        runner_->cancelActive();
    }

private:
    struct EngineTool {
        const char* id;
        const char* binary;
        const char* title;
    };

    static constexpr int SIGKILL_CODE = 9;

    static const std::vector<EngineTool>& tools() {
        static const std::vector<EngineTool> table{
            {"pdflatex", "pdflatex", "pdfLaTeX"},
            {"xelatex", "xelatex", "XeLaTeX"},
            {"lualatex", "lualatex", "LuaLaTeX"},
        };
        return table;
    }

    static std::string binaryFor(const std::string& engine_id) {
        for (const auto& tool : tools()) {
            if (engine_id == tool.id) {
                return tool.binary;
            }
        }
        return {};
    }

    static std::string envOverrideName(const std::string& engine_id) {
        std::string upper;
        for (const char c : engine_id) {
            upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
        return "LATEX_COMPILER_" + upper;
    }

    // Binary discovery: per-engine env override, injected search paths, PATH
    // and the well-known TeX Live install dirs (GUI apps get a minimal PATH).
    std::optional<std::string> locateBinary(const std::string& name) const {
        namespace fs = std::filesystem;
        if (name.empty()) {
            return std::nullopt;
        }
        const char* override_path = std::getenv(envOverrideName(name).c_str());
        if (override_path != nullptr && override_path[0] != '\0') {
            std::error_code ec;
            if (fs::is_regular_file(override_path, ec)) {
                return std::string{override_path};
            }
        }
        for (const auto& directory : searchDirectories()) {
            fs::path candidate(directory);
            candidate /= name
#ifdef _WIN32
                + ".exe"
#endif
                ;
            std::error_code ec;
            if (fs::is_regular_file(candidate, ec)) {
                return candidate.generic_string();
            }
        }
        return std::nullopt;
    }

    std::vector<std::string> searchDirectories() const {
        namespace fs = std::filesystem;
        if (replace_default_paths_) {
            std::vector<std::string> dirs = extra_search_paths_;
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
        std::vector<std::string> dirs = extra_search_paths_;

        // PATH entries keep the shell behaviour users expect.
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

        // TeX Live/MiKTeX install roots: expand one level of version dirs so
        // a GUI process finds the toolchain even with a minimal PATH.
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
        // De-duplicate while keeping order.
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

    // fontspec/unicode-math hard-fail under pdfLaTeX, so auto mode must
    // skip it when the document asks for a Unicode engine. Packages are
    // usually loaded from the preamble indirectly (\\input, path macros,
    // wrapper .sty), so the scan follows every local package/input it can
    // resolve inside the project and stops at its directory boundary.
    static bool documentRequiresUnicodeEngine(const std::filesystem::path& main_path) {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path root = fs::weakly_canonical(main_path.parent_path(), ec);
        std::set<fs::path> visited;
        return documentMentionsUnicodeEngine(main_path, root, visited);
    }

    // True when a document text loads fontspec or unicode-math, either
    // directly or by including one of its own local packages/inputs.
    static bool documentMentionsUnicodeEngine(const std::filesystem::path& file_path,
                                              const std::filesystem::path& project_root,
                                              std::set<std::filesystem::path>& visited) {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path canonical = fs::weakly_canonical(file_path, ec);
        if (!visited.insert(ec ? file_path : canonical).second) {
            return false;
        }
        std::ifstream input(file_path);
        if (!input) {
            return false;
        }
        const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (text.find("fontspec") != std::string::npos ||
            text.find("unicode-math") != std::string::npos) {
            return true;
        }
        for (const auto& included : localIncludes(text, project_root, file_path)) {
            if (documentMentionsUnicodeEngine(included, project_root, visited)) {
                return true;
            }
        }
        return false;
    }

    static std::string trimAscii(std::string value) {
        std::size_t begin = 0;
        while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
            ++begin;
        }
        std::size_t end = value.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
            --end;
        }
        return value.substr(begin, end - begin);
    }

    // Resolves \\usepackage/[RequirePackage] and \\input/[include] targets
    // that live inside the project (bare names search the project root, then
    // the including file's directory); system-wide files are ignored.
    static std::vector<std::filesystem::path> localIncludes(const std::string& text,
                                                            const std::filesystem::path& project_root,
                                                            const std::filesystem::path& including_file) {
        namespace fs = std::filesystem;
        static constexpr std::string_view markers[] = {"\\usepackage", "\\RequirePackage", "\\input", "\\include"};
        std::vector<fs::path> found;
        for (const std::string_view marker : markers) {
            std::size_t pos = 0;
            while ((pos = text.find(marker, pos)) != std::string::npos) {
                pos += marker.size();
                std::size_t cursor = pos;
                while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
                    ++cursor;
                }
                if (cursor >= text.size() || text[cursor] != '{') {
                    continue;
                }
                const std::size_t close = text.find('}', cursor + 1);
                if (close == std::string::npos) {
                    break;
                }
                // Comma lists (\\usepackage{a,b}) contribute every entry.
                std::size_t item = cursor + 1;
                while (item <= close) {
                    const std::size_t comma = text.find(',', item);
                    const std::size_t end = (comma == std::string::npos || comma > close) ? close : comma;
                    std::string name = trimAscii(text.substr(item, end - item));
                    item = end + 1;
                    if (name.empty() || name.find("..") != std::string::npos ||
                        name.front() == '/' || (name.size() > 1 && name[1] == ':')) {
                        continue;
                    }
                    for (const fs::path base : {project_root, including_file.parent_path()}) {
                        for (const std::string candidate : {name, name + ".tex", name + ".sty"}) {
                            std::error_code exists_ec;
                            const fs::path resolved = fs::weakly_canonical(base / candidate, exists_ec);
                            if (!exists_ec && fs::is_regular_file(resolved, exists_ec)) {
                                if (std::find(found.begin(), found.end(), resolved) == found.end()) {
                                    found.push_back(std::move(resolved));
                                }
                                break;
                            }
                        }
                    }
                }
                pos = close + 1;
            }
        }
        return found;
    }

    // "bibtex" when \bibliography{...} appears, "biber" for biblatex
    // (\addbibresource); empty when the document needs no bibliography pass.
    static std::string bibliographyToolFor(const std::filesystem::path& main_path) {
        std::ifstream input(main_path);
        if (!input) {
            return {};
        }
        std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (text.find("\\addbibresource") != std::string::npos) {
            return "biber";
        }
        if (text.find("\\bibliography{") != std::string::npos) {
            return "bibtex";
        }
        return {};
    }

    // Runs one engine pass. Returns false only when the run itself could
    // not happen or was killed (timeout/cancel); a nonzero exit with output
    // is reported in ok/exit_code because nonstopmode engines exit nonzero
    // after merely recovered errors while still writing the PDF.
    struct PassOutcome {
        bool ran = false;
        bool ok = false;
        int exit_code = -1;
    };
    PassOutcome runPass(const std::string& binary_path, const std::filesystem::path& working_dir,
                        const std::vector<std::string>& args, core::contracts::ICompileProgress* progress,
                        std::string& log) {
        core::contracts::ProcessRequest process_request;
        process_request.command = binary_path;
        process_request.args = args;
        process_request.working_directory = working_dir.generic_string();
        process_request.timeout_seconds = 300;

        // The log is captured unconditionally: error reporting depends on it
        // even when nobody is watching progress (sync API path).
        core::contracts::OutputSink sink = [progress, &log](const char* data, std::size_t size) {
            const std::string chunk(data, size);
            if (progress != nullptr) {
                progress->onOutput(chunk);
            }
            log += chunk;
            // Bound the captured log; only the tail matters for errors.
            if (log.size() > 200'000) {
                log.erase(0, log.size() - 100'000);
            }
        };

        const auto result = runner_->run(process_request, sink);
        if (result.timed_out) {
            log = "Compilation timed out.";
            return {true, false, result.exit_code};
        }
        if (result.exit_code < 0 || result.exit_code == 128 + SIGKILL_CODE) {
            log = "Compilation canceled.";
            return {true, false, result.exit_code};
        }
        return {true, result.exit_code == 0, result.exit_code};
    }

    static std::string logTail(const std::string& log, std::size_t max_chars) {
        if (log.size() <= max_chars) {
            return log;
        }
        return log.substr(log.size() - max_chars);
    }

    static core::contracts::CompileResult finish(core::contracts::ICompileProgress* progress,
                                                 core::contracts::CompileResult result) {
        if (progress != nullptr) {
            progress->onFinished(result);
        }
        return result;
    }

    std::shared_ptr<core::contracts::IProcessRunner> runner_;
    std::vector<std::string> extra_search_paths_;
    bool replace_default_paths_ = false;
};

} // namespace classtex
} // namespace adapters

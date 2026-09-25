#include "DiagramBuildService.hpp"

#include "BinaryLocator.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace core {
namespace fs = std::filesystem;
namespace {

// Same palette mapping the user's regenerate_diagrams.sh applies before
// rendering: light inline rgb() fills cannot be overridden by a dark Mermaid
// theme, so they are remapped to dark brand tints up front.
const std::map<std::string, std::string>& remapTable() {
    static const std::map<std::string, std::string> table = {
        {"rgb(255,245,230)", "rgb(46,40,24)"},
        {"rgb(255,240,200)", "rgb(46,40,24)"},
        {"rgb(255,235,235)", "rgb(52,26,28)"},
        {"rgb(255,230,230)", "rgb(52,26,28)"},
        {"rgb(255,220,220)", "rgb(52,26,28)"},
        {"rgb(255,220,200)", "rgb(52,26,28)"},
        {"rgb(245,240,255)", "rgb(30,27,58)"},
        {"rgb(240,240,255)", "rgb(30,27,58)"},
        {"rgb(220,200,255)", "rgb(30,27,58)"},
        {"rgb(230,255,230)", "rgb(22,50,30)"},
        {"rgb(240,255,240)", "rgb(22,50,30)"},
        {"rgb(220,255,220)", "rgb(22,50,30)"},
        {"rgb(200,240,200)", "rgb(22,50,30)"},
        {"rgb(230,245,255)", "rgb(16,34,56)"},
        {"rgb(220,235,255)", "rgb(16,34,56)"},
        {"rgb(240,248,255)", "rgb(16,34,56)"},
        {"rgb(220,240,255)", "rgb(16,34,56)"},
        {"rgb(200,220,255)", "rgb(16,34,56)"},
    };
    return table;
}

std::string normalizeRgbSpacing(const std::string& input) {
    static const std::string marker = "rgb(";
    std::string out;
    out.reserve(input.size());
    std::size_t i = 0;
    while (i < input.size()) {
        if (input.compare(i, marker.size(), marker) == 0) {
            const std::size_t close = input.find(')', i);
            if (close != std::string::npos && close - i <= 32) {
                std::string inner;
                for (std::size_t j = i + marker.size(); j < close; ++j) {
                    if (std::isspace(static_cast<unsigned char>(input[j])) == 0) {
                        inner += input[j];
                    }
                }
                // Only compact plain numeric rgb triples; leave others intact.
                bool numeric = !inner.empty();
                int commas = 0;
                for (const char c : inner) {
                    if (c == ',') {
                        ++commas;
                    } else if (c < '0' || c > '9') {
                        numeric = false;
                        break;
                    }
                }
                if (numeric && commas == 2) {
                    out += "rgb(" + inner + ")";
                    i = close + 1;
                    continue;
                }
            }
        }
        out += input[i];
        ++i;
    }
    return out;
}

std::string applyRemap(std::string input) {
    input = normalizeRgbSpacing(input);
    for (const auto& [from, to] : remapTable()) {
        std::size_t pos = 0;
        while ((pos = input.find(from, pos)) != std::string::npos) {
            input.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
    return input;
}

std::string readWholeFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void writeWholeFile(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

std::string lower(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string trim(const std::string& value) {
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

std::string executableName(const std::string& base) {
#ifdef _WIN32
    return base + ".cmd";
#else
    return base;
#endif
}

bool mentionsMissingChrome(const std::string& log) {
    return log.find("Could not find chrome") != std::string::npos ||
           log.find("chrome-headless-shell") != std::string::npos;
}

std::optional<std::string> findSystemChrome() {
#ifdef __APPLE__
    const std::vector<std::string> candidates = {
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        "/Applications/Chromium.app/Contents/MacOS/Chromium",
        "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
        "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser",
    };
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(fs::path(candidate), ec)) {
            return candidate;
        }
    }
    return std::nullopt;
#elif defined(_WIN32)
    const std::vector<std::string> candidates = {
        "C:/Program Files/Google/Chrome/Application/chrome.exe",
        "C:/Program Files (x86)/Google/Chrome/Application/chrome.exe",
        "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
    };
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(fs::path(candidate), ec)) {
            return candidate;
        }
    }
    return std::nullopt;
#else
    for (const char* name : {"google-chrome-stable", "google-chrome", "chromium-browser", "chromium"}) {
        if (auto found = BinaryLocator::find(name)) {
            return found;
        }
    }
    return std::nullopt;
#endif
}

// Marker next to the diagrams: identity of the toolchain that produced the
// current artifacts (see ToolchainIdentity in the header).
constexpr const char* kStampFile = ".latexcompiler-diagram-stamp";

// Raw clock ticks: full precision, self-comparable (the stamp only ever
// compares against identities computed on the same machine).
std::int64_t mtimeTicks(const fs::path& file) {
    std::error_code ec;
    const auto t = fs::last_write_time(file, ec);
    if (ec) {
        return 0;
    }
    return t.time_since_epoch().count();
}

} // namespace

DiagramBuildService::Summary DiagramBuildService::buildAll(const std::string& project_path) {
    Summary summary;
    const fs::path project = fs::path(project_path).lexically_normal();
    std::error_code ec;

    std::string toolchain_error;
    const auto tools = resolveTools(project, toolchain_error);

    std::vector<fs::path> sources;
    for (const auto& source_dir : options_.source_dirs) {
        const fs::path dir = project / source_dir;
        if (!fs::is_directory(dir, ec)) {
            continue;
        }
        for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec) && lower(it->path().extension().generic_string()) == ".mmd") {
                sources.push_back(it->path());
            }
        }
        ec.clear();
    }
    // Skip silently only when there is nothing to do; when diagram sources
    // exist but the toolchain could not be located, say why instead of
    // letting the engine fail on missing artifacts.
    if (sources.empty()) {
        return summary;
    }
    if (!tools) {
        summary.toolchain_error = toolchain_error;
        return summary;
    }
    std::sort(sources.begin(), sources.end());

    // Toolchain identity stamp: a changed renderer (mermaid/rsvg upgrade)
    // invalidates every cached artifact once — old outputs may predate the
    // current renderer's feature set (e.g. foreignObject labels that
    // librsvg silently drops). The new stamp is written only after a pass
    // without failures, so an interrupted rebuild retries completely.
    const ToolchainIdentity identity = currentIdentity(*tools);
    const bool stamp_ok = stampMatches(project, identity);
    summary.full_rebuild = !stamp_ok;

    const auto demands = collectDemands(project);
    const fs::path primary_dir = project / options_.source_dirs.front();
    const fs::path theme = primary_dir / options_.theme_config;
    const fs::path css = primary_dir / options_.css;
    const bool has_theme = fs::exists(theme, ec);
    const bool has_css = fs::exists(css, ec);

    // Puppeteer config produced by a one-time chrome recovery, applied to
    // every later mmdc run in this pass and removed on exit.
    std::string puppeteer_config;

    for (const auto& source : sources) {
        const std::string stem = source.stem().generic_string();
        std::set<std::string> formats;
        if (const auto it = demands.find(lower(stem)); it != demands.end()) {
            formats = it->second;
        }
        if (formats.empty()) {
            formats.insert("pdf");
        }

        for (const std::string& format : formats) {
            const fs::path target = source.parent_path() / (stem + "." + format);
            const fs::path theme_ref = has_theme ? theme : source;
            if (fs::exists(target, ec) && !summary.full_rebuild &&
                !fileNewerThan(source, target) && !fileNewerThan(theme_ref, target)) {
                ++summary.skipped;
                continue;
            }

            const fs::path temp_mmd = source.parent_path() / (stem + ".tmp.mmd");
            const fs::path temp_svg = source.parent_path() / (stem + ".tmp.svg");
            const fs::path mmdc_output = format == "svg" ? target : temp_svg;

            std::string mmd_input = source.generic_string();
            if (options_.color_remap) {
                writeWholeFile(temp_mmd, applyRemap(readWholeFile(source)));
                mmd_input = temp_mmd.generic_string();
            }

            std::vector<std::string> mmdc_args = {tools->mmdc, "-i", mmd_input, "-o", mmdc_output.generic_string(),
                                                  "-b", "transparent"};
            if (has_theme) {
                mmdc_args.push_back("-c");
                mmdc_args.push_back(theme.generic_string());
            }
            if (has_css) {
                mmdc_args.push_back("-C");
                mmdc_args.push_back(css.generic_string());
            }

            std::string log;
            bool ok = runTool(tools->node, mmdc_args, project_path, log);
            if (!ok && puppeteer_config.empty() && mentionsMissingChrome(log)) {
                // One-time recovery: reuse a system browser or install the
                // official chrome-headless-shell, then retry with its config.
                if (recoverChrome(project, *tools, puppeteer_config, log)) {
                    log.clear();
                    mmdc_args.push_back("-p");
                    mmdc_args.push_back(puppeteer_config);
                    ok = runTool(tools->node, mmdc_args, project_path, log);
                }
            }
            if (ok && format != "svg") {
                ok = runTool(tools->rsvg,
                             {"-f", format, "-o", target.generic_string(), temp_svg.generic_string()},
                             project_path, log);
            }

            std::error_code clean_ec;
            fs::remove(temp_mmd, clean_ec);
            fs::remove(temp_svg, clean_ec);

            if (ok) {
                ++summary.rendered;
            } else {
                ++summary.failed;
                if (summary.error.empty()) {
                    summary.error = stem + "." + format + ": " + trim(log);
                }
            }
        }
    }

    if (!puppeteer_config.empty()) {
        std::error_code rm_ec;
        fs::remove(puppeteer_config, rm_ec);
    }
    if (!stamp_ok && summary.failed == 0) {
        writeStamp(project, identity);
    }
    return summary;
}

std::map<std::string, std::set<std::string>> DiagramBuildService::collectDemands(const fs::path& project) const {
    std::map<std::string, std::set<std::string>> demands;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(project, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const std::string ext = lower(it->path().extension().generic_string());
        if (ext != ".tex" && ext != ".sty" && ext != ".cls") {
            continue;
        }
        const std::string content = readWholeFile(it->path());
        const std::string marker = "\\includegraphics";
        std::size_t pos = 0;
        while ((pos = content.find(marker, pos)) != std::string::npos) {
            std::size_t cursor = pos + marker.size();
            while (cursor < content.size() && std::isspace(static_cast<unsigned char>(content[cursor])) != 0) {
                ++cursor;
            }
            if (cursor < content.size() && content[cursor] == '[') {
                const std::size_t close = content.find(']', cursor);
                if (close == std::string::npos) {
                    break;
                }
                cursor = close + 1;
                while (cursor < content.size() && std::isspace(static_cast<unsigned char>(content[cursor])) != 0) {
                    ++cursor;
                }
            }
            if (cursor < content.size() && content[cursor] == '{') {
                const std::size_t close = content.find('}', cursor);
                if (close != std::string::npos) {
                    const std::string name = trim(content.substr(cursor + 1, close - cursor - 1));
                    const std::size_t slash = name.find_last_of("/\\");
                    const std::string file = slash == std::string::npos ? name : name.substr(slash + 1);
                    const std::string stem = lower(stripExtension(file));
                    const std::size_t dot = file.find_last_of('.');
                    if (dot != std::string::npos && dot != 0) {
                        // Explicit extension: only that format is demanded.
                        demands[stem].insert(lower(file.substr(dot + 1)));
                    } else if (!stem.empty()) {
                        // Extensionless \includegraphics: LaTeX picks the
                        // format from its driver-specific extension list, so
                        // produce PDF and SVG and let the engine choose.
                        demands[stem].insert("pdf");
                        demands[stem].insert("svg");
                    }
                }
            }
            pos += marker.size();
        }
    }
    return demands;
}

std::optional<DiagramBuildService::ToolPaths>
DiagramBuildService::resolveTools(const fs::path& project, std::string& missing) const {
    // Injected search paths first, then the project-local toolchain.
    std::vector<std::string> extra = extra_search_paths_;
    std::error_code ec;

    // Project-local mmdc wins: the project pins its mermaid-cli version.
    const std::string local_bin = (project / "node_modules" / ".bin").generic_string();
    if (fs::exists(project / "node_modules" / ".bin" / executableName("mmdc"), ec)) {
        extra.push_back(local_bin);
    }

    const std::string node = BinaryLocator::find(executableName("node"), extra, replace_default_paths_).value_or("");
    const auto mmdc_path = BinaryLocator::find(executableName("mmdc"), extra, replace_default_paths_);
    const auto rsvg_path = BinaryLocator::find(executableName("rsvg-convert"), extra, replace_default_paths_);
    auto name_missing = [](const bool absent, const std::string& name) {
        return absent ? (name.empty() ? name : name + " ") : std::string{};
    };
    missing = name_missing(node.empty(), "node") + name_missing(!mmdc_path, "mmdc") +
              name_missing(!rsvg_path, "rsvg-convert");
    if (!missing.empty()) {
        missing = "missing: " + missing.substr(0, missing.size() - 1);
    }
    if (node.empty() || !mmdc_path || !rsvg_path) {
        return std::nullopt;
    }

    ToolPaths tools;
    tools.node = node;
    tools.mmdc = *mmdc_path;
    tools.rsvg = *rsvg_path;
    return tools;
}

bool DiagramBuildService::runTool(const std::string& command, const std::vector<std::string>& args,
                                  const std::string& working_dir, std::string& log) const {
    contracts::ProcessRequest request;
    request.command = command;
    request.args = args;
    request.working_directory = working_dir;
    request.timeout_seconds = options_.timeout_seconds;

    std::string captured;
    const auto sink = [&captured](const char* data, std::size_t size) {
        captured.append(data, size);
    };
    const auto result = runner_->run(request, sink);
    if (!captured.empty()) {
        if (!log.empty()) {
            log += "\n";
        }
        log += captured;
    }
    if (result.timed_out) {
        log += "\ntimed out";
        return false;
    }
    return result.exit_code == 0;
}

bool DiagramBuildService::recoverChrome(const fs::path& project, const ToolPaths& tools,
                                        std::string& config_path, std::string& log) const {
    if (auto chrome = findSystemChrome()) {
        std::string escaped;
        for (const char c : *chrome) {
            if (c == '\\' || c == '"') {
                escaped += '\\';
            }
            escaped += c;
        }
        const fs::path config = project / ".latexcompiler-puppeteer.json";
        writeWholeFile(config, "{\"executablePath\":\"" + escaped + "\"}");
        config_path = config.generic_string();
        log += "\nrecovered with system browser: " + *chrome;
        return true;
    }

    std::vector<std::string> extra;
    std::error_code ec;
    const std::string local_bin = (project / "node_modules" / ".bin").generic_string();
    if (fs::exists(project / "node_modules" / ".bin" / executableName("puppeteer"), ec)) {
        extra.push_back(local_bin);
    }
    if (const auto puppeteer = BinaryLocator::find(executableName("puppeteer"), extra, replace_default_paths_)) {
        contracts::ProcessRequest request;
        request.command = tools.node;
        request.args = {*puppeteer, "browsers", "install", "chrome-headless-shell"};
        request.working_directory = project.generic_string();
        request.timeout_seconds = 900;
        const auto result = runner_->run(request);
        log += "\npuppeteer install: " + trim(result.std_out + " " + result.std_err);
        return result.exit_code == 0 && !result.timed_out;
    }
    log += "\nchrome missing and no puppeteer CLI found to install it";
    return false;
}

bool DiagramBuildService::fileNewerThan(const fs::path& file, const fs::path& reference) {
    std::error_code ec;
    const auto a = fs::last_write_time(file, ec);
    if (ec) {
        return false;
    }
    const auto b = fs::last_write_time(reference, ec);
    if (ec) {
        return false;
    }
    return a > b;
}

std::string DiagramBuildService::stripExtension(const std::string& file) {
    const std::size_t dot = file.find_last_of('.');
    return dot == std::string::npos ? file : file.substr(0, dot);
}

std::string DiagramBuildService::stampText(const ToolchainIdentity& identity) {
    std::ostringstream out;
    out << identity.mmdc_path << '|' << identity.mmdc_mtime << '|' << identity.rsvg_path
        << '|' << identity.rsvg_mtime;
    return out.str();
}

DiagramBuildService::ToolchainIdentity
DiagramBuildService::currentIdentity(const ToolPaths& tools) const {
    ToolchainIdentity identity;
    identity.mmdc_path = tools.mmdc;
    identity.mmdc_mtime = mtimeTicks(tools.mmdc);
    identity.rsvg_path = tools.rsvg;
    identity.rsvg_mtime = mtimeTicks(tools.rsvg);
    return identity;
}

bool DiagramBuildService::stampMatches(const fs::path& project, const ToolchainIdentity& identity) {
    std::ifstream in(project / kStampFile);
    if (!in) {
        return false;
    }
    std::string stored;
    std::getline(in, stored);
    return stored == stampText(identity);
}

void DiagramBuildService::writeStamp(const fs::path& project, const ToolchainIdentity& identity) {
    std::ofstream out(project / kStampFile, std::ios::trunc);
    out << stampText(identity);
}

} // namespace core

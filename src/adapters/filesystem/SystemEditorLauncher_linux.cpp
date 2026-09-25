#include "adapters/filesystem/SystemEditorLauncher.hpp"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#include <cstddef>

namespace adapters {
namespace filesystem {
namespace {

// Absolute path of name resolved on PATH, or empty when absent/not executable.
std::string which(const std::string& name) {
    const char* path_env = getenv("PATH");
    if (path_env == nullptr || path_env[0] == '\0') {
        return {};
    }
    std::string rest = path_env;
    while (!rest.empty()) {
        const std::string::size_type colon = rest.find(':');
        const std::string dir = colon == std::string::npos ? rest : rest.substr(0, colon);
        if (colon == std::string::npos) {
            rest.clear();
        } else {
            rest.erase(0, colon + 1);
        }
        if (dir.empty()) {
            continue;
        }
        const std::string candidate = dir + "/" + name;
        if (access(candidate.c_str(), X_OK) == 0) {
            return candidate;
        }
    }
    return {};
}

} // namespace

std::vector<core::contracts::EditorPreset> SystemEditorLauncher::presets() const {
    const bool has_xterm = !which("xterm").empty();

    auto preset = [&has_xterm](const std::string& id, const std::string& label,
                               const std::string& binary, bool requires_xterm) {
        const bool installed = !which(binary).empty() &&
                               (!requires_xterm || has_xterm);
        const std::string command = requires_xterm ? "xterm -e " + binary + " {file}"
                                                   : binary + " {file}";
        return core::contracts::EditorPreset{id, label, installed ? command : "", installed};
    };

    return {
        {"default", "App predeterminada", "", true},
        preset("gedit", "gedit", "gedit", false),
        preset("kate", "Kate", "kate", false),
        preset("code", "Visual Studio Code", "code", false),
        preset("vim", "Vim (terminal)", "vim", true),
        preset("neovim", "Neovim (terminal)", "nvim", true),
        {"custom", "Personalizado…", "", true},
    };
}

bool SystemEditorLauncher::open(const std::string& path, const std::string& command_template) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.empty() || !fs::exists(path, ec)) {
        return false;
    }

    const std::string absolute = fs::absolute(path).generic_string();
    if (command_template.empty()) {
        std::string quoted = "'";
        for (const char c : absolute) {
            if (c == '\'') {
                quoted += "'\\''";
            } else {
                quoted += c;
            }
        }
        quoted += "'";
        const std::string command = "xdg-open " + quoted + " >/dev/null 2>&1 &";
        return std::system(command.c_str()) == 0;
    }

    std::string quoted = "'";
    for (const char c : absolute) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";

    std::string command = command_template;
    const std::size_t slot = command.find("{file}");
    if (slot == std::string::npos) {
        return false;
    }
    command.replace(slot, 6, quoted);
    command += " >/dev/null 2>&1 &";
    return std::system(command.c_str()) == 0;
}

} // namespace filesystem
} // namespace adapters

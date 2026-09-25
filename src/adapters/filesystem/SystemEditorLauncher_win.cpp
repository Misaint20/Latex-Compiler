#include "adapters/filesystem/SystemEditorLauncher.hpp"

#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <filesystem>
#include <cstddef>
#include <string>
#include <vector>

namespace adapters {
namespace filesystem {
namespace {

std::string env_or_empty(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? value : "";
}

// Returns the full path of the first standard-root candidate that exists,
// or an empty string when the executable is not installed.
std::string find_exe(const std::string& exe_relative) {
    namespace fs = std::filesystem;
    const std::string roots[] = {
        env_or_empty("ProgramFiles"),
        env_or_empty("ProgramFiles(x86)"),
        env_or_empty("LOCALAPPDATA"),
    };
    for (const auto& root : roots) {
        if (root.empty()) {
            continue;
        }
        std::error_code ec;
        const std::string candidate = root + "\\" + exe_relative;
        if (fs::exists(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}

} // namespace

std::vector<core::contracts::EditorPreset> SystemEditorLauncher::presets() const {
    std::string vscode = find_exe("Programs\\Microsoft VS Code\\Code.exe");
    if (vscode.empty()) {
        vscode = find_exe("Microsoft VS Code\\Code.exe");
    }
    const std::string notepadpp = find_exe("Notepad++\\notepad++.exe");
    const std::string texstudio = find_exe("TeXstudio\\texstudio.exe");

    auto preset = [](const std::string& id, const std::string& label,
                     const std::string& exe) {
        const bool installed = !exe.empty();
        return core::contracts::EditorPreset{
            id, label, installed ? "\"" + exe + "\" {file}" : "", installed};
    };

    return {
        {"default", "App predeterminada", "", true},
        preset("vscode", "Visual Studio Code", vscode),
        preset("notepadpp", "Notepad++", notepadpp),
        preset("texstudio", "TeXstudio", texstudio),
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
        const std::wstring wide(absolute.begin(), absolute.end());
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", wide.c_str(),
                                               nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
    }

    const std::string quoted = "\"" + absolute + "\"";
    std::string command = command_template;
    const std::size_t slot = command.find("{file}");
    if (slot == std::string::npos) {
        return false;
    }
    command.replace(slot, 6, quoted);
    command += " > NUL 2>&1 &";
    return std::system(command.c_str()) == 0;
}

} // namespace filesystem
} // namespace adapters

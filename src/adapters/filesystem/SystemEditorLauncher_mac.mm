#import "adapters/filesystem/SystemEditorLauncher.hpp"

#import <Cocoa/Cocoa.h>

#include <cstdlib>
#include <filesystem>

namespace adapters {
namespace filesystem {

std::vector<core::contracts::EditorPreset> SystemEditorLauncher::presets() const {
    std::string home;
    if (const char* env_home = getenv("HOME")) {
        home = env_home;
    }
    const std::string app_dirs[] = {
        "/Applications",
        "/System/Applications",
        home + "/Applications",
    };

    auto installed = [&](const std::string& app_name) {
        const std::string bundle = app_name + ".app";
        for (const auto& dir : app_dirs) {
            std::error_code ec;
            if (std::filesystem::exists(dir + "/" + bundle, ec)) {
                return true;
            }
        }
        return false;
    };

    const std::string texshop = installed("TeXShop") ? "open -a TeXShop {file}" : "";
    const std::string texstudio = installed("TeXstudio") ? "open -a TeXstudio {file}" : "";
    const std::string vscode = installed("Visual Studio Code")
                                   ? "open -a \"Visual Studio Code\" {file}"
                                   : "";
    const std::string cursor = installed("Cursor") ? "open -a Cursor {file}" : "";
    const std::string sublime = installed("Sublime Text")
                                    ? "open -a \"Sublime Text\" {file}"
                                    : "";
    const std::string textmate = installed("TextMate") ? "open -a TextMate {file}" : "";
    const std::string bbedit = installed("BBEdit") ? "open -a BBEdit {file}" : "";

    return {
        {"default", "App predeterminada", "", true},
        {"texshop", "TeXShop", texshop, installed("TeXShop")},
        {"texstudio", "TeXstudio", texstudio, installed("TeXstudio")},
        {"vscode", "Visual Studio Code", vscode, installed("Visual Studio Code")},
        {"cursor", "Cursor", cursor, installed("Cursor")},
        {"sublime", "Sublime Text", sublime, installed("Sublime Text")},
        {"textmate", "TextMate", textmate, installed("TextMate")},
        {"bbedit", "BBEdit", bbedit, installed("BBEdit")},
        {"custom", "Personalizado…", "", true},
    };
}

bool SystemEditorLauncher::open(const std::string& path, const std::string& command_template) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.empty() || !fs::exists(path, ec)) {
        return false;
    }

    if (command_template.empty()) {
        @autoreleasepool {
            NSString* nsPath =
                [NSString stringWithUTF8String:fs::absolute(path).generic_string().c_str()];
            NSURL* url = [NSURL fileURLWithPath:nsPath];
            return url != nil && [[NSWorkspace sharedWorkspace] openURL:url];
        }
    }

    const std::string quoted = "'" + fs::absolute(path).generic_string() + "'";
    std::string command = command_template;
    const std::size_t slot = command.find("{file}");
    if (slot == std::string::npos) {
        return false;
    }
    command.replace(slot, 6, quoted);
    command += " > /dev/null 2>&1 &";
    return std::system(command.c_str()) == 0;
}

} // namespace filesystem
} // namespace adapters

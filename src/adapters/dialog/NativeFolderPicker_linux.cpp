#include "adapters/dialog/NativeFolderPicker.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <optional>

namespace adapters {
namespace dialog {
namespace {

std::string run_and_capture(const std::string& command, const std::vector<std::string>& args) {
    std::string full = command;
    for (const auto& arg : args) {
        std::string safe;
        for (const char c : arg) {
            if (c == '"' || c == '\\' || c == '$' || c == '`') {
                safe += '\\';
            }
            safe += c;
        }
        full += " \"" + safe + "\"";
    }
    full += " 2>/dev/null";

    std::array<char, 512> buffer{};
    std::string output;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(full.c_str(), "r"), pclose);
    if (!pipe) {
        return {};
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        output += buffer.data();
    }
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }
    return output;
}

} // namespace

std::optional<std::string> NativeFolderPicker::pickFolder(const std::string& title,
                                                          const std::string& start_path) {
    std::string chosen = run_and_capture("zenity", {"--file-selection", "--directory",
                                                    "--title", title,
                                                    "--filename", start_path});
    if (chosen.empty()) {
        chosen = run_and_capture("kdialog", {"--getexistingdirectory", start_path,
                                             "--title", title});
    }
    if (chosen.empty()) {
        return std::nullopt;
    }
    return std::optional{chosen};
}

} // namespace dialog
} // namespace adapters

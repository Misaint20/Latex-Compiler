#include "adapters/filesystem/SystemFileOpener.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace adapters {
namespace filesystem {

bool SystemFileOpener::open(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.empty() || !fs::exists(path, ec)) {
        return false;
    }
    std::string quoted = "\"";
    for (const char c : fs::absolute(path).generic_string()) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') {
            quoted += '\\';
        }
        quoted += c;
    }
    quoted += "\"";
    const std::string command = "xdg-open " + quoted + " >/dev/null 2>&1 &";
    return std::system(command.c_str()) == 0;
}

} // namespace filesystem
} // namespace adapters

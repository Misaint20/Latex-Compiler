#include "adapters/filesystem/SystemFileOpener.hpp"

#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

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
    const std::wstring wide(path.begin(), path.end());
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", wide.c_str(),
                                           nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

} // namespace filesystem
} // namespace adapters

#pragma once

#include "core/contracts/IFileOpener.hpp"

#include <string>

namespace adapters {
namespace filesystem {

// Opens files with the platform default handler: NSWorkspace on macOS,
// ShellExecute on Windows, xdg-open on Linux.
class SystemFileOpener : public core::contracts::IFileOpener {
public:
    bool open(const std::string& path) override;
};

} // namespace filesystem
} // namespace adapters

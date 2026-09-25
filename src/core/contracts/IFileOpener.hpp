#pragma once

#include <string>

namespace core {
namespace contracts {

class IFileOpener {
public:
    virtual ~IFileOpener() = default;
    // Asks the operating system to open the file with its default handler.
    // Returns false when the platform handler reports failure or is missing.
    virtual bool open(const std::string& path) = 0;
};

} // namespace contracts
} // namespace core

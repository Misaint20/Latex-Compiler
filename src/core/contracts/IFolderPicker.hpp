#pragma once

#include <optional>
#include <string>

namespace core {
namespace contracts {

class IFolderPicker {
public:
    virtual ~IFolderPicker() = default;
    // Returns the chosen folder, or std::nullopt when the user cancels.
    virtual std::optional<std::string> pickFolder(const std::string& title,
                                                  const std::string& start_path) = 0;
};

} // namespace contracts
} // namespace core

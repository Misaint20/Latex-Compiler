#pragma once

#include "core/contracts/IFolderPicker.hpp"

#include <optional>
#include <string>

namespace adapters {
namespace dialog {

// Opens the platform folder dialog. The blocking modal call happens on the
// main thread; the dispatcher routes it through the webview dispatch queue so
// the run loop keeps pumping events.
class NativeFolderPicker : public core::contracts::IFolderPicker {
public:
    std::optional<std::string> pickFolder(const std::string& title,
                                          const std::string& start_path) override;
};

} // namespace dialog
} // namespace adapters

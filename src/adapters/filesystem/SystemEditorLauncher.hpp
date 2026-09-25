#pragma once

#include "core/contracts/IEditorLauncher.hpp"

#include <string>
#include <vector>

namespace adapters {
namespace filesystem {

// Launches editors from per-platform presets or a custom command template.
// The custom template must contain "{file}", which is replaced by the quoted
// absolute path. An empty template delegates to the system default handler.
class SystemEditorLauncher : public core::contracts::IEditorLauncher {
public:
    std::vector<core::contracts::EditorPreset> presets() const override;

    bool open(const std::string& path, const std::string& command_template) override;
};

} // namespace filesystem
} // namespace adapters

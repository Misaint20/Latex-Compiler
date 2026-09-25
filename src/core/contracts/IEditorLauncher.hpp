#pragma once

#include <string>
#include <vector>

namespace core {
namespace contracts {

struct EditorPreset {
    std::string id;     // Stable identifier stored in settings.
    std::string label;  // Human-readable name shown in the UI.
    std::string command; // Launch template containing "{file}".
    // True when the editor was detected on this machine; false lets the UI
    // dim the option. Non-app entries ("default", "custom") are always true.
    bool installed = true;
};

class IEditorLauncher {
public:
    virtual ~IEditorLauncher() = default;

    // Platform-specific editor presets; empty command means "default handler".
    // Presets report whether their editor is installed on this machine.
    virtual std::vector<EditorPreset> presets() const = 0;

    // True when the launcher can check installation status (platform
    // supports detection). Platforms without a probe keep presets enabled.
    virtual bool available() const { return true; }

    // Launches the editor by replacing "{file}" in the template with the
    // properly quoted absolute path. An empty template opens the file with
    // the platform default handler.
    virtual bool open(const std::string& path, const std::string& command_template) = 0;
};

} // namespace contracts
} // namespace core

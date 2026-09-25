#pragma once

#include "contracts/IAppSettings.hpp"
#include "contracts/IEditorLauncher.hpp"
#include "contracts/IEventPublisher.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <utility>

namespace core {

// Owns per-file-type editor preferences: lists platform presets, remembers
// the user's choice across launches, and launches files accordingly. Scope
// "editor" selects .tex-like sources; scope "diagram" selects .mmd files.
// Installation probes are cached in settings with a TTL; an expired cache is
// served immediately and refreshed on a background thread (the probe scans
// disk and must not block IPC responses).
class EditorService {
public:
    // Which file family a preference applies to.
    enum class Scope { Editor, Diagram };

    // How long a cached probe stays trusted.
    static constexpr std::chrono::seconds kProbeTtl{300};

    EditorService(std::shared_ptr<contracts::IAppSettings> settings,
                std::shared_ptr<contracts::IEditorLauncher> launcher,
                std::shared_ptr<contracts::IEventPublisher> eventPublisher = nullptr,
                std::chrono::seconds probe_ttl = kProbeTtl);

    // Available editors for the running platform, with install status per
    // preset so the UI can dim the missing ones.
    std::vector<contracts::EditorPreset> presets() const;

    // True when the launcher can probe installation status on this platform.
    bool available() const;

    // The stored choice for a scope: preset id, "custom", or empty (= system
    // default handler).
    std::string preferred(Scope scope = Scope::Editor) const;

    // The stored custom template for a scope (only meaningful when the scope
    // choice is "custom").
    std::string customTemplate(Scope scope = Scope::Editor) const;

    // Persists the choice for a scope: preset id, or "custom" with template.
    void setPreferred(Scope scope,
                    const std::string& preset_id,
                    const std::string& custom_template);

    // Opens an in-project file with the preferred editor for its scope.
    // Editor scope: .tex/.sty/.cls/.bib. Diagram scope: .mmd. Files outside
    // both scopes fall back to the system default handler.
    bool openInProject(const std::string& project_path, const std::string& relative_path);

    // Opens an in-project file with an explicit preset, without touching the
    // stored preference. Empty preset_id opens the system default handler;
    // "custom" uses the stored custom template; an unknown or uninstalled
    // preset id falls back to the system default handler.
    bool openWithPreset(const std::string& project_path,
                        const std::string& relative_path,
                        const std::string& preset_id);

    // Drops the persisted presets cache (including any in-flight background
    // refresh) so the next presets() call re-probes the disk synchronously.
    // Exposed for tests and manual refresh; used by the hidden IPC method.
    void invalidatePresetsCache();

    ~EditorService();

    // Test hooks: injectable time source for TTL expiry, and a barrier that
    // waits for any in-flight background refresh to finish.
    void setClock(std::function<std::int64_t()> clock) { clock_ = std::move(clock); }
    void waitUntilIdle() const {
        if (refresh_thread_.joinable()) {
            refresh_thread_.join();
        }
    }

private:
    std::optional<contracts::EditorPreset> find_preset(const std::string& preset_id) const;

    std::vector<contracts::EditorPreset> cached_presets(bool& fresh) const;
    void store_cached_presets(const std::vector<contracts::EditorPreset>& presets) const;
    std::vector<contracts::EditorPreset> probe_now() const;
    void start_background_refresh(const std::vector<contracts::EditorPreset>& stale) const;

    std::shared_ptr<contracts::IAppSettings> settings_;
    std::shared_ptr<contracts::IEditorLauncher> launcher_;
    std::shared_ptr<contracts::IEventPublisher> eventPublisher_;
    std::chrono::seconds probe_ttl_;

    // One refresh at a time; the thread outlives call frames, so join before
    // destruction touches members.
    mutable std::atomic<bool> refresh_in_flight_{false};
    mutable std::thread refresh_thread_;

    // Test hook: injectable time source for TTL expiry.
    mutable std::function<std::int64_t()> clock_;
};

} // namespace core

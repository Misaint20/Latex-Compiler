#include "EditorService.hpp"
#include "ProjectPaths.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <utility>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace core
{
    namespace
    {

        // Settings keys are per scope: editor.* for .tex-like sources and
        // diagram.* for .mmd files. The presets cache is shared by both scopes.
        constexpr const char *kEditorKey = "editor.preferred";
        constexpr const char *kTemplateKey = "editor.customTemplate";
        constexpr const char *kDiagramKey = "diagram.preferred";
        constexpr const char *kDiagramTemplateKey = "diagram.customTemplate";
        constexpr const char *kCacheKey = "editor.presets.cache";

        bool is_tex_file(const std::string &path)
        {
            const std::size_t dot = path.rfind('.');
            if (dot == std::string::npos)
            {
                return false;
            }
            std::string ext = path.substr(dot);
            for (char &c : ext)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return ext == ".tex" || ext == ".sty" || ext == ".cls" || ext == ".bib";
        }

        bool is_diagram_file(const std::string &path)
        {
            const std::size_t dot = path.rfind('.');
            if (dot == std::string::npos)
            {
                return false;
            }
            std::string ext = path.substr(dot);
            for (char &c : ext)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return ext == ".mmd";
        }

        std::int64_t now_ms()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                .count();
        }

        struct ScopeKeys
        {
            const char *preferred;
            const char *custom_template;
        };

        ScopeKeys scope_keys(EditorService::Scope scope)
        {
            switch (scope)
            {
            case EditorService::Scope::Diagram:
                return {kDiagramKey, kDiagramTemplateKey};
            case EditorService::Scope::Editor:
            default:
                return {kEditorKey, kTemplateKey};
            }
        }

    } // namespace

    EditorService::EditorService(std::shared_ptr<contracts::IAppSettings> settings,
                                std::shared_ptr<contracts::IEditorLauncher> launcher,
                                std::shared_ptr<contracts::IEventPublisher> eventPublisher,
                                std::chrono::seconds probe_ttl)
        : settings_(std::move(settings)),
        launcher_(std::move(launcher)),
        eventPublisher_(std::move(eventPublisher)),
        probe_ttl_(probe_ttl),
        clock_(now_ms) {}

    EditorService::~EditorService()
    {
        if (refresh_thread_.joinable())
        {
            refresh_thread_.join();
        }
    }

    std::vector<contracts::EditorPreset> EditorService::presets() const
    {
        if (!launcher_)
        {
            return {};
        }

        bool fresh = false;
        auto cached = cached_presets(fresh);
        if (fresh)
        {
            return cached;
        }

        if (!cached.empty())
        {
            // Stale cache: serve it and refresh without blocking the caller.
            start_background_refresh(cached);
            return cached;
        }

        // No cache yet: probe synchronously, then persist.
        auto probed = probe_now();
        store_cached_presets(probed);
        return probed;
    }

    void EditorService::start_background_refresh(
        const std::vector<contracts::EditorPreset> &stale) const
    {
        bool expected = false;
        if (!refresh_in_flight_.compare_exchange_strong(expected, true))
        {
            return;
        }
        if (refresh_thread_.joinable())
        {
            refresh_thread_.join();
        }
        refresh_thread_ = std::thread([this, stale]()
                                    {
        auto probed = probe_now();
        bool changed = probed.size() != stale.size();
        for (std::size_t i = 0; changed && i < probed.size() && i < stale.size(); ++i) {
            if (probed[i].id != stale[i].id || probed[i].installed != stale[i].installed) {
                changed = true;
            }
        }
        store_cached_presets(probed);
        refresh_in_flight_.store(false);
        if (eventPublisher_ && changed) {
            eventPublisher_->publish({"editor.presetsChanged", "{}"});
        } });
    }

    std::vector<contracts::EditorPreset> EditorService::probe_now() const
    {
        return launcher_->presets();
    }

    std::vector<contracts::EditorPreset> EditorService::cached_presets(bool &fresh) const
    {
        fresh = false;
        if (!settings_)
        {
            return {};
        }
        const std::string blob = settings_->get(kCacheKey, "");
        if (blob.empty())
        {
            return {};
        }
        try
        {
            const auto document = nlohmann::json::parse(blob);
            const std::int64_t cached_at = document.value("cachedAt", std::int64_t{0});
            if (clock_() - cached_at < 0)
            {
                // Clock moved backwards (timezone/NTP correction): distrust.
                return {};
            }
            fresh = clock_() - cached_at < probe_ttl_.count() * 1000;
            std::vector<contracts::EditorPreset> presets;
            for (const auto &item : document.value("presets", nlohmann::json::array()))
            {
                contracts::EditorPreset preset;
                preset.id = item.value("id", std::string{});
                preset.label = item.value("label", std::string{});
                preset.command = item.value("command", std::string{});
                preset.installed = item.value("installed", true);
                if (!preset.id.empty())
                {
                    presets.push_back(std::move(preset));
                }
            }
            return presets;
        }
        catch (const std::exception &)
        {
            // Corrupt cache behaves like no cache.
            return {};
        }
    }

    void EditorService::store_cached_presets(
        const std::vector<contracts::EditorPreset> &presets) const
    {
        if (!settings_)
        {
            return;
        }
        auto list = nlohmann::json::array();
        for (const auto &preset : presets)
        {
            list.push_back({
                {"id", preset.id},
                {"label", preset.label},
                {"command", preset.command},
                {"installed", preset.installed},
            });
        }
        const std::string blob = nlohmann::json{
            {"cachedAt", clock_()},
            {"presets", list},
        }
                                     .dump();
        settings_->set(kCacheKey, blob);
    }

    bool EditorService::available() const
    {
        return launcher_ != nullptr && launcher_->available();
    }

    void EditorService::invalidatePresetsCache()
    {
        // A refresh thread writes the cache when it finishes; it must be gone
        // before the key is cleared, or it would re-persist stale data.
        if (refresh_thread_.joinable())
        {
            refresh_thread_.join();
        }
        refresh_in_flight_.store(false);
        if (settings_)
        {
            // An empty value reads back as "no cache".
            settings_->set(kCacheKey, "");
        }
    }

    std::string EditorService::preferred(Scope scope) const
    {
        if (!settings_)
        {
            return {};
        }
        return settings_->get(scope_keys(scope).preferred, "");
    }

    std::string EditorService::customTemplate(Scope scope) const
    {
        if (!settings_)
        {
            return {};
        }
        return settings_->get(scope_keys(scope).custom_template, "");
    }

    void EditorService::setPreferred(Scope scope,
                                    const std::string &preset_id,
                                    const std::string &custom_template)
    {
        if (!settings_)
        {
            return;
        }
        const auto keys = scope_keys(scope);
        if (preset_id == "custom")
        {
            settings_->set(keys.preferred, "custom");
            settings_->set(keys.custom_template, custom_template);
            return;
        }
        if (preset_id.empty())
        {
            settings_->set(keys.preferred, "");
            return;
        }
        const auto preset = find_preset(preset_id);
        if (preset.has_value() && preset->installed)
        {
            settings_->set(keys.preferred, preset_id);
        }
    }

    bool EditorService::openInProject(const std::string &project_path,
                                    const std::string &relative_path)
    {
        const auto resolved = resolve_inside_project(project_path, relative_path);
        if (!resolved || !launcher_)
        {
            return false;
        }

        const bool diagram = is_diagram_file(*resolved);
        if (!is_tex_file(*resolved) && !diagram)
        {
            // PNG, PDF, etc. always go to the system default handler.
            return launcher_->open(*resolved, "");
        }
        const Scope scope = diagram ? Scope::Diagram : Scope::Editor;
        const std::string choice = preferred(scope);
        if (choice.empty())
        {
            return launcher_->open(*resolved, "");
        }
        if (choice == "custom")
        {
            return launcher_->open(*resolved, customTemplate(scope));
        }
        const auto preset = find_preset(choice);
        if (!preset.has_value() || !preset->installed)
        {
            // The stored preset no longer exists on this platform; fall back.
            return launcher_->open(*resolved, "");
        }
        return launcher_->open(*resolved, preset->command);
    }

    bool EditorService::openWithPreset(const std::string &project_path,
                                    const std::string &relative_path,
                                    const std::string &preset_id)
    {
        const auto resolved = resolve_inside_project(project_path, relative_path);
        if (!resolved || !launcher_)
        {
            return false;
        }
        if (preset_id.empty())
        {
            return launcher_->open(*resolved, "");
        }
        if (preset_id == "custom")
        {
            return launcher_->open(*resolved, customTemplate(Scope::Editor));
        }
        const auto preset = find_preset(preset_id);
        if (!preset.has_value() || !preset->installed)
        {
            return launcher_->open(*resolved, "");
        }
        return launcher_->open(*resolved, preset->command);
    }

    std::optional<contracts::EditorPreset> EditorService::find_preset(
        const std::string &preset_id) const
    {
        if (!launcher_)
        {
            return std::nullopt;
        }
        for (const auto &preset : launcher_->presets())
        {
            if (preset.id == preset_id)
            {
                return preset;
            }
        }
        return std::nullopt;
    }

} // namespace core

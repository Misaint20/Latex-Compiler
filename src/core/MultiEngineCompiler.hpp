#pragma once

#include "contracts/IAppSettings.hpp"
#include "contracts/ICompiler.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace core {

// Chooses among the engines contributed by the composed adapters. Engine ids
// are "<engine>@<backend>" so identical tool names from different adapters
// stay distinguishable (e.g. a future latexmk backend). The setting key
// "compile.engine" stores either "auto" or an explicit engine id.
class MultiEngineCompiler : public contracts::ICompiler {
public:
    struct Entry {
        std::string backend;
        std::shared_ptr<contracts::ICompiler> adapter;
    };

    MultiEngineCompiler(std::vector<Entry> adapters,
                        std::shared_ptr<contracts::IAppSettings> settings = nullptr)
        : adapters_(std::move(adapters)), settings_(std::move(settings)) {}

    std::vector<contracts::EngineInfo> availableEngines() override {
        std::vector<contracts::EngineInfo> engines;
        for (const auto& entry : adapters_) {
            for (auto engine : entry.adapter->availableEngines()) {
                engine.id = engine.id + "@" + entry.backend;
                engines.push_back(std::move(engine));
            }
        }
        return engines;
    }

    contracts::CompileResult compile(const contracts::CompileRequest& request) override {
        return compile(request, nullptr);
    }

    contracts::CompileResult compile(const contracts::CompileRequest& request,
                                     contracts::ICompileProgress* progress) override {
        const std::string stored = storedPreference();
        // Explicit request id wins over the stored preference.
        const std::string requested = !request.engine.empty() ? request.engine : stored;
        const bool explicit_pick = !requested.empty() && requested != "auto";

        std::vector<EngineRef> chain = planChain(requested);
        if (chain.empty()) {
            return {false, {}, "No LaTeX engine is installed. Install TeX Live (pdflatex) or Tectonic and try again.", {}};
        }

        bool attempted_any = false;
        for (std::size_t index = 0; index < chain.size(); ++index) {
            auto& candidate = chain[index];
            // Missing engines cannot run; skip them instead of surfacing the
            // adapter's not-found error as a compile failure.
            if (!candidate.adapter || candidate.missing) {
                continue;
            }
            attempted_any = true;
            setActiveAdapter(candidate.adapter);
            // Forward the picked engine so adapters do not ignore an explicit
            // selection (the catalogue id carries a "@backend" suffix the
            // adapter does not know). In auto mode the id stays empty: the
            // adapter then selects per document (fontspec rules out pdfLaTeX),
            // which a fixed chain order cannot know.
            contracts::CompileRequest scoped = request;
            if (explicit_pick && index == 0 && chain.front().displayName == requested) {
                const std::size_t at = candidate.displayName.rfind('@');
                scoped.engine = at == std::string::npos
                                    ? candidate.displayName
                                    : candidate.displayName.substr(0, at);
            }
            contracts::CompileResult result = candidate.adapter->compile(scoped, progress);
            if (result.success) {
                if (index > 0) {
                    result.notice = "Engine '" + chain.front().displayName +
                                    "' was not available; compiled with '" + candidate.displayName + "' instead.";
                }
                return result;
            }
            // A real compile failure surfaces as-is so genuine errors are
            // visible; only unavailable engines fall through the chain.
            return result;
        }
        if (!attempted_any) {
            return {false, {}, "No LaTeX engine is installed. Install TeX Live (pdflatex, xelatex or lualatex) or Tectonic and try again.", {}};
        }
        return {false, {}, "No LaTeX engine could run this project.", {}};
    }    void cancel(contracts::CompileId id) override {
        std::shared_ptr<contracts::ICompiler> active;
        {
            std::lock_guard<std::mutex> lock(active_mutex_);
            active = active_adapter_.lock();
        }
        if (active) {
            active->cancel(std::move(id));
            return;
        }
        for (auto& entry : adapters_) {
            entry.adapter->cancel({});
        }
    }

    // Persisted preference: "auto" or an explicit engine id.
    std::string preferredEngine() const {
        return settings_ ? settings_->get(kEngineSettingKey, "auto") : "auto";
    }

    void setPreferredEngine(const std::string& engine_id) {
        if (settings_) {
            settings_->set(kEngineSettingKey, engine_id.empty() ? "auto" : engine_id);
        }
    }

    // True for "auto" or any engine id the adapters currently expose.
    bool isKnownEngine(const std::string& engine_id) {
        if (engine_id.empty() || engine_id == "auto") {
            return true;
        }
        for (const auto& engine : availableEngines()) {
            if (engine.id == engine_id) {
                return true;
            }
        }
        return false;
    }

private:
    struct EngineRef {
        std::shared_ptr<contracts::ICompiler> adapter;
        std::string displayName;
        bool missing = false;
    };

    static constexpr const char* kEngineSettingKey = "compile.engine";

    std::string storedPreference() const {
        return settings_ ? settings_->get(kEngineSettingKey, "auto") : "auto";
    }

    // Builds the engine attempt order: the requested engine first (marked
    // when its adapter reports it missing), then the remaining installed
    // engines in canonical order for auto fallback.
    std::vector<EngineRef> planChain(const std::string& requested) {
        std::vector<EngineRef> chain;
        const auto engines = availableEngines();

        auto append = [&](const std::string& id, bool mark_missing) {
            for (const auto& engine : engines) {
                if (engine.id != id) {
                    continue;
                }
                if (std::any_of(chain.begin(), chain.end(),
                                [&](const EngineRef& ref) { return ref.displayName == id; })) {
                    return;
                }
                chain.push_back({adapterFor(id), id, mark_missing && !engine.available});
                return;
            }
        };

        // Canonical order: classic engines first, tectonic as last resort.
        static const char* order[] = {
            "pdflatex", "xelatex", "lualatex", "tectonic",
        };

        if (!requested.empty() && requested != "auto") {
            append(requested, true);
            // Manual picks do not silently fall back to other engines.
            if (!chain.empty() && !chain.front().missing) {
                return chain;
            }
        }
        for (const char* id : order) {
            for (const auto& engine : engines) {
                if (engine.id.rfind(std::string(id) + "@", 0) == 0 && engine.available) {
                    append(engine.id, false);
                    break;
                }
            }
        }
        return chain;
    }

    std::shared_ptr<contracts::ICompiler> adapterFor(const std::string& engine_id) const {
        const std::size_t at = engine_id.rfind('@');
        const std::string backend = at == std::string::npos ? engine_id : engine_id.substr(at + 1);
        for (const auto& entry : adapters_) {
            if (entry.backend == backend) {
                return entry.adapter;
            }
        }
        return nullptr;
    }

    void setActiveAdapter(const std::shared_ptr<contracts::ICompiler>& adapter) {
        std::lock_guard<std::mutex> lock(active_mutex_);
        active_adapter_ = adapter;
    }

    std::vector<Entry> adapters_;
    std::shared_ptr<contracts::IAppSettings> settings_;

    std::mutex active_mutex_;
    std::weak_ptr<contracts::ICompiler> active_adapter_;
};

} // namespace core

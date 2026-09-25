#include "core/EditorService.hpp"
#include "ipc/request_dispatcher.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

class MemorySettings : public core::contracts::IAppSettings {
public:
    std::string get(const std::string& key, const std::string& fallback) override {
        const auto found = values.find(key);
        return found == values.end() ? fallback : found->second;
    }

    void set(const std::string& key, const std::string& value) override {
        values[key] = value;
    }

    std::map<std::string, std::string> values;
};

class RecordingEvents : public core::contracts::IEventPublisher {
public:
    void publish(const core::contracts::Event& event) override {
        topics.push_back(event.topic);
    }

    std::vector<std::string> topics;
};

class StubLauncher : public core::contracts::IEditorLauncher {
public:
    std::vector<core::contracts::EditorPreset> presets() const override {
        ++presets_calls;
        auto list = std::vector<core::contracts::EditorPreset>{
            {"default", "App predeterminada", ""},
            {"fake", "Fake Editor", "fake-edit {file}"},
        };
        list.insert(list.end(), extra_presets.begin(), extra_presets.end());
        return list;
    }

    bool available() const override { return available_flag; }

    bool open(const std::string& path, const std::string& command_template) override {
        ++open_calls;
        last_path = path;
        last_template = command_template;
        return next_result;
    }

    std::vector<core::contracts::EditorPreset> extra_presets;
    bool available_flag = true;
    mutable int presets_calls = 0;
    int open_calls = 0;
    std::string last_path;
    std::string last_template;
    bool next_result = true;
};

} // namespace

TEST_CASE("EditorService stores and clears the preferred preset") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    core::EditorService service(settings, launcher);

    // Unknown preset ids are rejected.
    service.setPreferred(core::EditorService::Scope::Editor, "nope", "");
    CHECK(service.preferred().empty());

    service.setPreferred(core::EditorService::Scope::Editor, "fake", "");
    CHECK(service.preferred() == "fake");

    service.setPreferred(core::EditorService::Scope::Editor, "", "");
    CHECK(service.preferred().empty());
}

TEST_CASE("EditorService keeps a custom template only in custom mode") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    core::EditorService service(settings, launcher);

    service.setPreferred(core::EditorService::Scope::Editor, "custom", "code {file}");
    CHECK(service.preferred() == "custom");
    CHECK(service.customTemplate() == "code {file}");

    // Switching to a preset keeps the template stored but unused.
    service.setPreferred(core::EditorService::Scope::Editor, "fake", "");
    CHECK(service.customTemplate() == "code {file}");
}

TEST_CASE("EditorService opens tex files with the preferred editor") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    core::EditorService service(settings, launcher);

    service.setPreferred(core::EditorService::Scope::Editor, "fake", "");
    CHECK(service.openInProject("/tmp/proj", "capitulos/intro.tex"));
    CHECK(launcher->open_calls == 1);
    CHECK(launcher->last_template == "fake-edit {file}");
    CHECK(launcher->last_path.find("capitulos/intro.tex") != std::string::npos);
}

TEST_CASE("EditorService falls back to the default handler when no preference") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    core::EditorService service(settings, launcher);

    CHECK(service.openInProject("/tmp/proj", "capitulos/intro.tex"));
    CHECK(launcher->open_calls == 1);
    CHECK(launcher->last_template.empty());
}

TEST_CASE("EditorService sends non-tex files to the default handler") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    core::EditorService service(settings, launcher);

    service.setPreferred(core::EditorService::Scope::Editor, "fake", "");
    CHECK(service.openInProject("/tmp/proj", "figuras/logo.png"));
    CHECK(launcher->open_calls == 1);
    CHECK(launcher->last_template.empty());
}

TEST_CASE("EditorService falls back when the stored preset vanished") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    core::EditorService service(settings, launcher);

    settings->values["editor.preferred"] = "ghost-editor";
    CHECK(service.openInProject("/tmp/proj", "main.tex"));
    CHECK(launcher->open_calls == 1);
    CHECK(launcher->last_template.empty());
}

TEST_CASE("EditorService opens custom template with the file slot filled") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    core::EditorService service(settings, launcher);

    service.setPreferred(core::EditorService::Scope::Editor, "custom", "myeditor --line {file}");
    CHECK(service.openInProject("/tmp/proj", "main.tex"));
    CHECK(launcher->last_template == "myeditor --line {file}");
}

TEST_CASE("EditorService rejects traversal before launching anything") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    core::EditorService service(settings, launcher);

    service.setPreferred(core::EditorService::Scope::Editor, "fake", "");
    CHECK_FALSE(service.openInProject("/tmp/proj", "../../etc/passwd"));
    CHECK(launcher->open_calls == 0);
}

TEST_CASE("EditorService works without settings or launcher") {
    core::EditorService service(nullptr, nullptr);
    CHECK(service.preferred().empty());
    CHECK(service.presets().empty());
    CHECK(service.openInProject("/tmp/proj", "main.tex") == false);
}

TEST_CASE("EditorService rejects uninstalled presets and falls back to default") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    launcher->extra_presets.push_back({"missing", "Missing Editor", "missing {file}", false});
    core::EditorService service(settings, launcher);

    service.setPreferred(core::EditorService::Scope::Editor, "missing", "");
    CHECK(service.preferred().empty());

    // A choice stored before the editor was uninstalled still falls back.
    settings->values["editor.preferred"] = "missing";
    CHECK(service.openInProject("/tmp/proj", "main.tex"));
    CHECK(launcher->open_calls == 1);
    CHECK(launcher->last_template.empty());
}

TEST_CASE("invalidatePresetsCache forces a synchronous re-probe and clears in-flight state") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();

    std::atomic<std::int64_t> now{3'000'000};
    core::EditorService service(settings, launcher, nullptr, std::chrono::seconds{300});
    service.setClock([&now]() { return now.load(); });

    CHECK_FALSE(service.presets().empty());
    CHECK(launcher->presets_calls == 1);

    // Inside the TTL everything is served from the cache.
    CHECK_FALSE(service.presets().empty());
    CHECK(launcher->presets_calls == 1);

    // Invalidation drops the cache: the very next presets() re-probes now,
    // synchronously, without waiting for any TTL or background refresh.
    service.invalidatePresetsCache();
    CHECK_FALSE(service.presets().empty());
    CHECK(launcher->presets_calls == 2);

    // The fresh cache is stored again and reused within the TTL.
    CHECK_FALSE(service.presets().empty());
    CHECK(launcher->presets_calls == 2);
}

TEST_CASE("invalidatePresetsCache survives a fresh probe followed by invalidation") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    core::EditorService service(settings, launcher, nullptr, std::chrono::seconds{300});

    CHECK_FALSE(service.presets().empty());
    CHECK(launcher->presets_calls == 1);

    service.invalidatePresetsCache();
    CHECK(settings->values["editor.presets.cache"].empty());

    // Next call re-probes (call 2) and re-persists a fresh cache.
    CHECK_FALSE(service.presets().empty());
    CHECK(launcher->presets_calls == 2);
    CHECK_FALSE(settings->values["editor.presets.cache"].empty());
}

TEST_CASE("invalidatePresetsCache without settings or launcher does not crash") {
    core::EditorService service(nullptr, nullptr);
    service.invalidatePresetsCache();
}

TEST_CASE("editor.invalidatePresetsCache over IPC clears the cache") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    std::atomic<std::int64_t> now{1'000'000};
    core::EditorService service(settings, launcher, nullptr, std::chrono::seconds{300});
    service.setClock([&now]() { return now.load(); });
    CHECK_FALSE(service.presets().empty());

    ipc::RequestDispatcher dispatcher(nullptr, nullptr, nullptr, std::shared_ptr<core::EditorService>(&service, [](core::EditorService*) {}));
    const auto response = nlohmann::json::parse(dispatcher.dispatch(
        R"([{"version":1,"id":"90","method":"editor.invalidatePresetsCache","params":{}}])"));
    CHECK(response["ok"] == true);
    CHECK(response["result"]["invalidated"] == true);
    CHECK(settings->values["editor.presets.cache"].empty());
}

TEST_CASE("openWithPreset launches explicitly without changing the preference") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    core::EditorService service(settings, launcher);

    // A stored preference that must remain untouched by openWithPreset.
    service.setPreferred(core::EditorService::Scope::Editor, "fake", "");

    SUBCASE("empty preset id opens the system default handler") {
        CHECK(service.openWithPreset("/tmp/proj", "diagramas/flujo.mmd", ""));
        CHECK(launcher->last_template.empty());
        CHECK(service.preferred() == "fake");
    }

    SUBCASE("known installed preset launches with its command") {
        CHECK(service.openWithPreset("/tmp/proj", "diagramas/flujo.mmd", "fake"));
        CHECK(launcher->last_template == "fake-edit {file}");
        CHECK(service.preferred() == "fake");
    }

    SUBCASE("unknown preset falls back to the system default handler") {
        CHECK(service.openWithPreset("/tmp/proj", "diagramas/flujo.mmd", "ghost"));
        CHECK(launcher->last_template.empty());
        CHECK(service.preferred() == "fake");
    }

    SUBCASE("traversal is rejected before launching anything") {
        CHECK_FALSE(service.openWithPreset("/tmp/proj", "../outside.mmd", "fake"));
        CHECK(launcher->open_calls == 0);
        CHECK(service.preferred() == "fake");
    }
}

TEST_CASE("openWithPreset custom uses the stored template") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    core::EditorService service(settings, launcher);
    service.setPreferred(core::EditorService::Scope::Editor, "custom", "subl {file}");

    CHECK(service.openWithPreset("/tmp/proj", "diagramas/flujo.mmd", "custom"));
    CHECK(launcher->last_template == "subl {file}");
}

TEST_CASE("openWithPreset uninstalled preset falls back to the default handler") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    launcher->extra_presets.push_back({"gone", "Gone Editor", "gone {file}", false});
    core::EditorService service(settings, launcher);

    CHECK(service.openWithPreset("/tmp/proj", "diagramas/flujo.mmd", "gone"));
    CHECK(launcher->last_template.empty());
}

TEST_CASE("EditorService mirrors launcher availability") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    core::EditorService service(settings, launcher);

    CHECK(service.available());
    launcher->available_flag = false;
    CHECK_FALSE(service.available());

    core::EditorService without_launcher(nullptr, nullptr);
    CHECK_FALSE(without_launcher.available());
}

TEST_CASE("EditorService caches the installation probe and serves it within TTL") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    core::EditorService service(settings, launcher, nullptr, std::chrono::seconds{300});

    CHECK(launcher->presets_calls == 0);
    CHECK_FALSE(service.presets().empty());
    CHECK(launcher->presets_calls == 1);

    // Second call inside the TTL is served from the cache.
    CHECK_FALSE(service.presets().empty());
    CHECK(launcher->presets_calls == 1);
}

TEST_CASE("EditorService re-probes after TTL expiry and persists the refresh") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();

    std::atomic<std::int64_t> now{1'000'000};
    core::EditorService service(settings, launcher, nullptr, std::chrono::seconds{300});
    service.setClock([&now]() { return now.load(); });

    CHECK_FALSE(service.presets().empty());
    CHECK(launcher->presets_calls == 1);

    // Advance past the TTL: stale cache is served, probe reruns on a worker.
    now.store(1'000'000 + 301'000);
    CHECK_FALSE(service.presets().empty());
    CHECK(launcher->presets_calls == 1);
    service.waitUntilIdle();
    CHECK(launcher->presets_calls == 2);

    // The refreshed cache is trusted again without another probe.
    CHECK_FALSE(service.presets().empty());
    CHECK(launcher->presets_calls == 2);
}

TEST_CASE("EditorService publishes presetsChanged when the refresh differs") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    auto events = std::make_shared<RecordingEvents>();

    std::atomic<std::int64_t> now{2'000'000};
    core::EditorService service(settings, launcher, events, std::chrono::seconds{300});
    service.setClock([&now]() { return now.load(); });

    launcher->extra_presets.push_back({"missing", "Missing Editor", "", false});
    CHECK_FALSE(service.presets().empty());
    service.waitUntilIdle();

    now.store(2'000'000 + 400'000);
    CHECK_FALSE(service.presets().empty());
    service.waitUntilIdle();

    // Uninstall the editor behind the cache's back, then expire the TTL.
    launcher->extra_presets.clear();
    now.store(2'000'000 + 900'000);
    CHECK_FALSE(service.presets().empty());
    service.waitUntilIdle();

    REQUIRE(events->topics.size() == 1);
    CHECK(events->topics[0] == "editor.presetsChanged");

    // A second identical expiry publishes nothing new.
    now.store(2'000'000 + 1'400'000);
    CHECK_FALSE(service.presets().empty());
    service.waitUntilIdle();
    CHECK(events->topics.size() == 1);
}

TEST_CASE("EditorService keeps diagram scope preferences separate") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    launcher->extra_presets.push_back({"mmdc", "Mermaid CLI", "mmdc {file}", true});
    core::EditorService service(settings, launcher);

    service.setPreferred(core::EditorService::Scope::Editor, "fake", "");
    service.setPreferred(core::EditorService::Scope::Diagram, "mmdc", "");

    CHECK(service.preferred(core::EditorService::Scope::Editor) == "fake");
    CHECK(service.preferred(core::EditorService::Scope::Diagram) == "mmdc");

    // Unknown preset ids are rejected per scope.
    service.setPreferred(core::EditorService::Scope::Diagram, "nope", "");
    CHECK(service.preferred(core::EditorService::Scope::Diagram) == "mmdc");

    // Clearing one scope does not affect the other.
    service.setPreferred(core::EditorService::Scope::Editor, "", "");
    CHECK(service.preferred(core::EditorService::Scope::Editor).empty());
    CHECK(service.preferred(core::EditorService::Scope::Diagram) == "mmdc");
}

TEST_CASE("EditorService opens mmd with the diagram preference") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();
    core::EditorService service(settings, launcher);

    launcher->extra_presets.push_back({"mmdc", "Mermaid CLI", "mmdc {file}", true});
    service.setPreferred(core::EditorService::Scope::Diagram, "mmdc", "");

    CHECK(service.openInProject("/tmp/proj", "diagramas/arquitectura.mmd"));
    CHECK(launcher->open_calls == 1);
    CHECK(launcher->last_template == "mmdc {file}");

    // No diagram preference means the system default handler.
    service.setPreferred(core::EditorService::Scope::Diagram, "", "");
    CHECK(service.openInProject("/tmp/proj", "diagramas/arquitectura.mmd"));
    CHECK(launcher->last_template.empty());

    // Custom template fills the {file} slot for diagrams too.
    service.setPreferred(core::EditorService::Scope::Diagram, "custom", "code {file}");
    CHECK(service.openInProject("/tmp/proj", "d.mmd"));
    CHECK(launcher->last_template == "code {file}");

    // Non-tex, non-diagram files ignore preferences entirely.
    service.setPreferred(core::EditorService::Scope::Diagram, "mmdc", "");
    CHECK(service.openInProject("/tmp/proj", "figuras/logo.png"));
    CHECK(launcher->last_template.empty());
}

TEST_CASE("EditorService distrusts future-dated or corrupt caches") {
    auto settings = std::make_shared<MemorySettings>();
    auto launcher = std::make_shared<StubLauncher>();

    std::atomic<std::int64_t> now{3'000'000};
    core::EditorService service(settings, launcher, nullptr, std::chrono::seconds{300});
    service.setClock([&now]() { return now.load(); });

    service.presets();
    CHECK(launcher->presets_calls == 1);

    // Clock rolled back before the cache date: the cache is worthless.
    now.store(2'999'000);
    CHECK_FALSE(service.presets().empty());
    service.waitUntilIdle();
    CHECK(launcher->presets_calls == 2);

    settings->values["editor.presets.cache"] = "not json at all";
    CHECK_FALSE(service.presets().empty());
    CHECK(launcher->presets_calls == 3);
}

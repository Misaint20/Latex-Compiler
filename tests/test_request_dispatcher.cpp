#include "adapters/filesystem/SystemProjectScanner.hpp"
#include "core/CompileService.hpp"
#include "core/DiagramService.hpp"
#include "core/EditorService.hpp"
#include "core/ProjectService.hpp"
#include "core/contracts/IAppSettings.hpp"
#include "core/contracts/IEditorLauncher.hpp"
#include "core/contracts/IFileOpener.hpp"
#include "core/contracts/IFolderPicker.hpp"
#include "core/contracts/IRecentProjects.hpp"
#include "ipc/request_dispatcher.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <cstddef>
#include <fstream>
#include <map>
#include <system_error>
#include <memory>
#include <utility>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

class StubCompiler : public core::contracts::ICompiler {
public:
    std::vector<core::contracts::EngineInfo> availableEngines() override {
        return {{"stub", "Stub", true}};
    }

    core::contracts::CompileResult compile(const core::contracts::CompileRequest& request) override {
        return compile(request, nullptr);
    }

    core::contracts::CompileResult compile(const core::contracts::CompileRequest& request,
                                           core::contracts::ICompileProgress*) override {
        last = request;
        if (fail) {
            return {false, {}, "boom"};
        }
        return {true, "/tmp/project/build/main.pdf", {}};
    }
    void cancel(core::contracts::CompileId) override { cancel_calls++; }
    core::contracts::CompileRequest last;
    bool fail = false;
    int cancel_calls = 0;
};

class StubFileStore : public core::contracts::IFileStore {
public:
    std::string readFile(const std::string&) override { return {}; }
    void writeFile(const std::string&, const std::string&) override {}
    bool exists(const std::string&) const override { return true; }
    std::vector<std::string> listDirectory(const std::string&) const override { return {}; }
};

class StubEvents : public core::contracts::IEventPublisher {
public:
    void publish(const core::contracts::Event&) override {}
};

class StubScanner : public core::contracts::IProjectScanner {
public:
    core::contracts::ProjectInfo scan(const std::string&) override {
        return info;
    }
    core::contracts::ProjectInfo info{"/tmp/project", {"main.tex", "chapters/intro.tex"}, "main.tex"};
};

class StubFileStoreReader : public core::contracts::IFileStore {
public:
    std::string readFile(const std::string& path) override {
        last_read = path;
        const auto canonical = std::filesystem::weakly_canonical(path).generic_string();
        return files.count(canonical) ? files[canonical] : throw std::runtime_error("missing");
    }
    void writeFile(const std::string& path, const std::string& content) override {
        ++write_calls;
        last_write_path = path;
        last_write_content = content;
        files[path] = content;
    }
    bool exists(const std::string& path) const override {
        return files.count(std::filesystem::weakly_canonical(path).generic_string()) > 0;
    }
    std::vector<std::string> listDirectory(const std::string&) const override { return {}; }

    std::unordered_map<std::string, std::string> files;
    int write_calls = 0;
    std::string last_write_path;
    std::string last_write_content;
    mutable std::string last_read;
};

class StubRenderer : public core::contracts::IDiagramRenderer {
public:
    core::contracts::RenderedDiagram render(const core::contracts::DiagramSource& source,
                                            const core::contracts::RenderOptions&) override {
        return {true, "<svg>" + source.code + "</svg>", {}};
    }
};

class StubCache : public core::contracts::IDiagramCache {
public:
    std::optional<std::string> get(const std::string& key) override {
        const auto found = entries.find(key);
        return found == entries.end() ? std::nullopt : std::optional{found->second};
    }
    void set(const std::string& key, const std::string& value) override {
        entries[key] = value;
    }
    std::unordered_map<std::string, std::string> entries;
};

class StubRecents : public core::contracts::IRecentProjects {
public:
    std::vector<core::contracts::RecentProject> list() override {
        return entries;
    }

    void record(const std::string& path) override {
        ++record_calls;
        last_recorded = path;
        // Mirror the real adapter: re-recording keeps the remembered main file
        // and last tab.
        std::string remembered;
        std::string remembered_tab;
        for (const auto& entry : entries) {
            if (entry.path == path) {
                remembered = entry.main_file;
                remembered_tab = entry.last_tab;
                break;
            }
        }
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&](const auto& e) { return e.path == path; }),
                      entries.end());
        entries.insert(entries.begin(), {path, 1'000, true, remembered, remembered_tab});
    }

    bool remove(const std::string& path) override {
        ++remove_calls;
        const auto before = entries.size();
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&](const auto& e) { return e.path == path; }),
                      entries.end());
        return entries.size() != before;
    }

    std::size_t removeMissing() override {
        ++purge_calls;
        return purge_entries();
    }

    std::size_t removeMissingForceProbe() override {
        ++purge_calls;
        return purge_entries();
    }

private:
    std::size_t purge_entries() {
        const auto before = entries.size();
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&](const auto& e) { return !e.exists; }),
                      entries.end());
        return before - entries.size();
    }

public:
    void rememberMainFile(const std::string& path, const std::string& main_file) override {
        ++remember_calls;
        last_remembered = {path, main_file};
        for (auto& entry : entries) {
            if (entry.path == path) {
                entry.main_file = main_file;
                return;
            }
        }
        entries.insert(entries.begin(), {path, 1'000, true, main_file});
    }

    void rememberLastTab(const std::string& path, const std::string& tab) override {
        ++remember_tab_calls;
        for (auto& entry : entries) {
            if (entry.path == path) {
                entry.last_tab = tab;
                return;
            }
        }
    }

    std::vector<core::contracts::RecentProject> entries;
    int record_calls = 0;
    int remove_calls = 0;
    int purge_calls = 0;
    int remember_calls = 0;
    int remember_tab_calls = 0;
    std::pair<std::string, std::string> last_remembered;
    std::string last_recorded;
};

class StubFileOpener : public core::contracts::IFileOpener {
public:
    bool open(const std::string& path) override {
        ++open_calls;
        last_path = path;
        return next_result;
    }

    std::string last_path;
    bool next_result = true;
    int open_calls = 0;
};

class StubFolderPicker : public core::contracts::IFolderPicker {
public:
    std::optional<std::string> pickFolder(const std::string& title,
                                          const std::string& start_path) override {
        ++pick_calls;
        last_title = title;
        last_start = start_path;
        if (next_result.empty()) {
            return std::nullopt;
        }
        return std::optional{next_result};
    }

    std::string next_result;
    std::string last_title;
    std::string last_start;
    int pick_calls = 0;
};

class StubEditorLauncher : public core::contracts::IEditorLauncher {
public:
    std::vector<core::contracts::EditorPreset> presets() const override {
        return {
            {"default", "App predeterminada", "", true},
            {"fake", "Fake Editor", "fake-edit {file}", true},
            {"missing", "Missing Editor", "", false},
        };
    }

    bool open(const std::string&, const std::string&) override { return true; }
};

class MemorySettings : public core::contracts::IAppSettings {
public:
    std::string get(const std::string& key, const std::string& fallback) override {
        const auto it = values.find(key);
        return it == values.end() ? fallback : it->second;
    }
    void set(const std::string& key, const std::string& value) override {
        values[key] = value;
    }

    std::map<std::string, std::string> values;
};

struct Fixture {
    std::shared_ptr<StubCompiler> compiler = std::make_shared<StubCompiler>();
    std::shared_ptr<StubFileStore> files = std::make_shared<StubFileStore>();
    std::shared_ptr<StubEvents> events = std::make_shared<StubEvents>();
    std::shared_ptr<StubScanner> scanner = std::make_shared<StubScanner>();
    std::shared_ptr<StubRenderer> renderer = std::make_shared<StubRenderer>();
    std::shared_ptr<StubCache> cache = std::make_shared<StubCache>();
    std::shared_ptr<StubFolderPicker> picker = std::make_shared<StubFolderPicker>();
    std::shared_ptr<StubFileStoreReader> reader = std::make_shared<StubFileStoreReader>();
    std::shared_ptr<StubFileOpener> opener = std::make_shared<StubFileOpener>();
    std::shared_ptr<StubRecents> recents = std::make_shared<StubRecents>();
    std::shared_ptr<StubEditorLauncher> editor_launcher = std::make_shared<StubEditorLauncher>();
    std::shared_ptr<MemorySettings> settings = std::make_shared<MemorySettings>();

    std::shared_ptr<ipc::RequestDispatcher> dispatcher() {
        auto compile_service = std::make_shared<core::CompileService>(compiler, files, events, opener);
        auto project_service = std::make_shared<core::ProjectService>(
            scanner, picker, reader, opener, recents);
        auto diagram_service = std::make_shared<core::DiagramService>(renderer, cache);
        auto editor_service = std::make_shared<core::EditorService>(settings, editor_launcher);
        auto instance = std::make_shared<ipc::RequestDispatcher>(
            compile_service, project_service, diagram_service, editor_service);
        instance->setReplySink([this](std::string /*binding_id*/, std::string response) {
            {
                std::lock_guard<std::mutex> lock(reply_mutex);
                replies.push_back(std::move(response));
            }
            reply_cv.notify_one();
        });
        dispatcher_ref = instance;
        return instance;
    }

    // Waits for one async reply; fails the test when none arrives in time.
    nlohmann::json waitReply() {
        std::unique_lock<std::mutex> lock(reply_mutex);
        if (!reply_cv.wait_for(lock, std::chrono::seconds(5),
                               [this] { return !replies.empty(); })) {
            FAIL("no async reply arrived");
        }
        auto response = nlohmann::json::parse(replies.front());
        replies.erase(replies.begin());
        return response;
    }

    std::mutex reply_mutex;
    std::condition_variable reply_cv;
    std::vector<std::string> replies;
    std::weak_ptr<ipc::RequestDispatcher> dispatcher_ref;
};

} // namespace

TEST_CASE("dispatcher answers app.ping") {
    Fixture fx;
    const auto response = nlohmann::json::parse(fx.dispatcher()->dispatch(
        R"([{"version":1,"id":"1","method":"app.ping","params":{}}])"));

    CHECK(response["ok"] == true);
    CHECK(response["result"]["message"] == "pong");
}

TEST_CASE("fast handlers deliver replies through the sync sink when a binding id is present") {
    Fixture fx;
    auto dispatcher = fx.dispatcher();

    std::mutex sync_mutex;
    std::condition_variable sync_cv;
    std::vector<std::pair<std::string, std::string>> sync_replies;
    dispatcher->setSyncReplySink([&](std::string binding_id, std::string response) {
        std::lock_guard<std::mutex> lock(sync_mutex);
        sync_replies.emplace_back(std::move(binding_id), std::move(response));
        sync_cv.notify_one();
    });

    const auto response = nlohmann::json::parse(dispatcher->dispatch(
        R"([{"version":1,"id":"fast-1","method":"app.ping","params":{}}])",
        "binding-7"));
    CHECK(response["ok"] == true);

    std::unique_lock<std::mutex> lock(sync_mutex);
    CHECK(sync_cv.wait_for(lock, std::chrono::seconds(5),
                           [&] { return !sync_replies.empty(); }));
    REQUIRE(sync_replies.size() == 1);
    CHECK(sync_replies.front().first == "binding-7");
    const auto sunk = nlohmann::json::parse(sync_replies.front().second);
    CHECK(sunk["id"] == "fast-1");
    CHECK(sunk["result"]["message"] == "pong");
}

TEST_CASE("app.setProjectMenuEnabled routes to the injected enabler") {
    Fixture fx;
    auto dispatcher = fx.dispatcher();
    bool last_state = true;
    bool called = false;
    dispatcher->setProjectMenuEnabler([&](bool enabled) {
        called = true;
        last_state = enabled;
    });

    const auto on = nlohmann::json::parse(dispatcher->dispatch(
        R"([{"version":1,"id":"1","method":"app.setProjectMenuEnabled","params":{"enabled":true}}])"));
    CHECK(on["ok"] == true);
    CHECK(called);
    CHECK(last_state == true);

    const auto off = nlohmann::json::parse(dispatcher->dispatch(
        R"([{"version":1,"id":"2","method":"app.setProjectMenuEnabled","params":{"enabled":false}}])"));
    CHECK(off["ok"] == true);
    CHECK(last_state == false);
}

TEST_CASE("compiler engine metadata and preference methods") {
    Fixture fx;
    const auto engines = nlohmann::json::parse(fx.dispatcher()->dispatch(
        R"([{"version":1,"id":"1","method":"compiler.engines","params":{}}])"));
    CHECK(engines["ok"] == true);
    CHECK(engines["result"]["engines"].is_array());
    CHECK(engines["result"]["engines"].size() == 1);
    CHECK(engines["result"]["engines"][0]["id"] == "stub");
    CHECK(engines["result"]["preferred"] == "auto");

    const auto saved = nlohmann::json::parse(fx.dispatcher()->dispatch(
        R"([{"version":1,"id":"2","method":"compiler.setEngine","params":{"engine":"auto"}}])"));
    CHECK(saved["ok"] == true);
    CHECK(saved["result"]["saved"] == true);
}

TEST_CASE("dispatcher routes compiler.compile to the compile service") {
    Fixture fx;
    const auto response = nlohmann::json::parse(fx.dispatcher()->dispatch(
        R"([{"version":1,"id":"2","method":"compiler.compile",)"
        R"("params":{"projectPath":"/tmp/project","mainFile":"main.tex"}}])"));

    CHECK(response["ok"] == true);
    CHECK(response["result"]["success"] == true);
    CHECK(fx.compiler->last.mainFile == "main.tex");
}

TEST_CASE("dispatcher routes project.scan") {
    Fixture fx;
    const auto response = nlohmann::json::parse(fx.dispatcher()->dispatch(
        R"([{"version":1,"id":"3","method":"project.scan",)"
        R"("params":{"projectPath":"/tmp/project"}}])"));

    CHECK(response["ok"] == true);
    CHECK(response["result"]["mainFileCandidate"] == "main.tex");
    CHECK(response["result"]["texFiles"].size() == 2);
}

TEST_CASE("dispatcher rejects unknown methods and malformed requests") {
    Fixture fx;
    const auto unknown = nlohmann::json::parse(fx.dispatcher()->dispatch(
        R"([{"version":1,"id":"4","method":"nope","params":{}}])"));
    CHECK(unknown["ok"] == false);
    CHECK(unknown["error"]["code"] == "unknown_method");

    const auto malformed = nlohmann::json::parse(fx.dispatcher()->dispatch("not json"));
    CHECK(malformed["ok"] == false);
    CHECK(malformed["error"]["code"] == "invalid_json");

    const auto bad_version = nlohmann::json::parse(fx.dispatcher()->dispatch(
        R"([{"version":99,"id":"5","method":"app.ping","params":{}}])"));
    CHECK(bad_version["ok"] == false);
    CHECK(bad_version["error"]["code"] == "unsupported_version");
}

TEST_CASE("project.pickFolder returns the chosen path and cancellation") {
    Fixture fx;
    const auto dispatcher = fx.dispatcher();
    const auto request_for = [](const std::string& start) {
        return R"([{"version":1,"id":"11","method":"project.pickFolder","params":{"startPath":")" +
               start + R"("}}])";
    };

    SUBCASE("user picks a folder") {
        fx.picker->next_result = "/picked/path";
        dispatcher->dispatch(request_for(""));
        const auto response = fx.waitReply();
        CHECK(response["ok"] == true);
        CHECK(response["result"]["path"] == "/picked/path");
        CHECK(fx.picker->pick_calls == 1);
    }

    SUBCASE("user cancels the dialog") {
        fx.picker->next_result.clear();
        dispatcher->dispatch(request_for(""));
        const auto response = fx.waitReply();
        CHECK(response["ok"] == true);
        CHECK(response["result"]["path"] == "");
        CHECK(fx.picker->pick_calls == 1);
    }
}

TEST_CASE("project.readProjectFileBinary serves previewable assets and rejects the rest") {
    Fixture fx;
    const auto dispatcher = fx.dispatcher();
    const auto request_for = [](const std::string& relative) {
        return R"([{"version":1,"id":"19","method":"project.readProjectFileBinary",)"
               R"("params":{"projectPath":"/tmp/proj","relativePath":")" + relative + R"("}}])";
    };

    SUBCASE("serves a PNG with its MIME type") {
        const auto key = std::filesystem::weakly_canonical("/tmp/proj/figs/plot.png").generic_string();
        fx.reader->files[key] = std::string("\x89PNG\r\n\x1a\n", 8);
        dispatcher->dispatch(request_for("figs/plot.png"));
        const auto response = fx.waitReply();
        CHECK(response["ok"] == true);
        CHECK(response["result"]["mime"] == "image/png");
        CHECK(response["result"]["dataBase64"] == "iVBORw0KGgo=");
    }

    SUBCASE("serves a PDF with its MIME type") {
        const auto key = std::filesystem::weakly_canonical("/tmp/proj/paper.pdf").generic_string();
        fx.reader->files[key] = "%PDF-1.7";
        dispatcher->dispatch(request_for("paper.pdf"));
        const auto response = fx.waitReply();
        CHECK(response["ok"] == true);
        CHECK(response["result"]["mime"] == "application/pdf");
        CHECK(response["result"]["dataBase64"] == "JVBERi0xLjc=");
    }

    SUBCASE("rejects traversal outside the project root") {
        dispatcher->dispatch(request_for("../../etc/evil.png"));
        const auto response = fx.waitReply();
        CHECK(response["ok"] == false);
        CHECK(response["error"]["code"] == "file_not_found");
    }

    SUBCASE("rejects non-previewable extensions") {
        dispatcher->dispatch(request_for("main.tex"));
        const auto response = fx.waitReply();
        CHECK(response["ok"] == false);
        CHECK(response["error"]["code"] == "file_not_found");
    }
}

TEST_CASE("project.readFile serves in-project files and rejects traversal") {
    Fixture fx;
    const auto dispatcher = fx.dispatcher();
    const auto request_for = [](const std::string& relative) {
        return R"([{"version":1,"id":"12","method":"project.readFile",)"
               R"("params":{"projectPath":"/tmp/proj","relativePath":")" + relative +
               R"("}}])";
    };

    SUBCASE("reads a diagram inside the project") {
        const auto key = std::filesystem::weakly_canonical("/tmp/proj/diagramas/flujo.mmd").generic_string();
        fx.reader->files[key] = "graph TD; A-->B;";
        dispatcher->dispatch(request_for("diagramas/flujo.mmd"));
        const auto response = fx.waitReply();
        CHECK(response["ok"] == true);
        CHECK(response["result"]["content"] == "graph TD; A-->B;");
    }

    SUBCASE("rejects traversal outside the project root") {
        fx.reader->files["/etc/passwd"] = "secret";
        dispatcher->dispatch(request_for("../../etc/passwd"));
        const auto response = fx.waitReply();
        CHECK(response["ok"] == false);
        CHECK(response["error"]["code"] == "file_not_found");
    }

    SUBCASE("rejects missing files") {
        dispatcher->dispatch(request_for("nope.mmd"));
        const auto response = fx.waitReply();
        CHECK(response["ok"] == false);
        CHECK(response["error"]["code"] == "file_not_found");
    }
}

TEST_CASE("project.rememberLastTab persists and project.scan surfaces it") {
    Fixture fx;
    const auto canonical_project = std::filesystem::weakly_canonical("/tmp/project").generic_string();
    fx.recents->entries = {{canonical_project, 1'000, true, "main.tex", "diagrams"}};
    fx.scanner->info.projectPath = canonical_project;
    const auto dispatcher = fx.dispatcher();

    SUBCASE("routes rememberLastTab through the service") {
        const auto response = nlohmann::json::parse(dispatcher->dispatch(
            R"([{"version":1,"id":"45","method":"project.rememberLastTab",)"
            R"("params":{"projectPath":")" + canonical_project + R"(","tab":"styles"}}])"));
        CHECK(response["ok"] == true);
        CHECK(response["result"]["saved"] == true);
        CHECK(fx.recents->remember_tab_calls == 1);
        CHECK(fx.recents->entries[0].last_tab == "styles");
    }

    SUBCASE("scan surfaces the remembered tab") {
        const auto response = nlohmann::json::parse(dispatcher->dispatch(
            R"([{"version":1,"id":"46","method":"project.scan",)"
            R"("params":{"projectPath":")" + canonical_project + R"("}}])"));
        REQUIRE(response["ok"] == true);
        CHECK(response["result"]["savedLastTab"] == "diagrams");
    }

    SUBCASE("recentProjects includes lastTab") {
        const auto response = nlohmann::json::parse(dispatcher->dispatch(
            R"([{"version":1,"id":"47","method":"project.recentProjects","params":{}}])"));
        REQUIRE(response["ok"] == true);
        CHECK(response["result"]["projects"][0]["lastTab"] == "diagrams");
    }
}

TEST_CASE("project.writeProjectFile saves editable text sources and rejects bad writes") {
    Fixture fx;
    const auto dispatcher = fx.dispatcher();
    const auto request_for = [](const std::string& relative, const std::string& content) {
        return R"([{"version":1,"id":"15","method":"project.writeProjectFile",)"
               R"("params":{"projectPath":"/tmp/proj","relativePath":")" + relative +
               R"(","content":")" + content + R"("}}])";
    };

    SUBCASE("saves a diagram inside the project") {
        const auto response = nlohmann::json::parse(
            dispatcher->dispatch(request_for("diagramas/flujo.mmd", "graph TD; A-->B;")));
        CHECK(response["ok"] == true);
        CHECK(response["result"]["saved"] == true);
        CHECK(fx.reader->write_calls == 1);
        CHECK(fx.reader->last_write_content == "graph TD; A-->B;");
    }

    SUBCASE("rejects traversal outside the project root") {
        const auto response = nlohmann::json::parse(
            dispatcher->dispatch(request_for("../../etc/evil.mmd", "x")));
        CHECK(response["ok"] == false);
        CHECK(response["error"]["code"] == "write_failed");
        CHECK(fx.reader->write_calls == 0);
    }

    SUBCASE("saves a chapter inside the project") {
        const auto response = nlohmann::json::parse(
            dispatcher->dispatch(request_for("capitulos/intro.tex", "intro content")));
        CHECK(response["ok"] == true);
        CHECK(response["result"]["saved"] == true);
        CHECK(fx.reader->write_calls == 1);
        CHECK(fx.reader->last_write_content == "intro content");
    }

    SUBCASE("rejects non-editable extensions") {
        const auto response = nlohmann::json::parse(
            dispatcher->dispatch(request_for("estilos/main.sty", "x")));
        CHECK(response["ok"] == false);
        CHECK(response["error"]["code"] == "write_failed");
        CHECK(fx.reader->write_calls == 0);
    }
}

TEST_CASE("project.openFile and compiler.openOutput reach the opener") {
    Fixture fx;
    const auto dispatcher = fx.dispatcher();

    SUBCASE("opens a project file inside the root") {
        const auto response = nlohmann::json::parse(dispatcher->dispatch(
            R"([{"version":1,"id":"13","method":"project.openFile",)"
            R"("params":{"projectPath":"/tmp/proj","relativePath":"capitulos/intro.tex"}}])"));
        CHECK(response["ok"] == true);
        CHECK(response["result"]["opened"] == true);
        CHECK(fx.opener->open_calls == 1);
        CHECK(fx.opener->last_path.find("capitulos/intro.tex") != std::string::npos);
    }

    SUBCASE("rejects traversal") {
        const auto response = nlohmann::json::parse(dispatcher->dispatch(
            R"([{"version":1,"id":"14","method":"project.openFile",)"
            R"("params":{"projectPath":"/tmp/proj","relativePath":"../secret.txt"}}])"));
        CHECK(response["ok"] == false);
        CHECK(response["error"]["code"] == "open_failed");
        CHECK(fx.opener->open_calls == 0);
    }

    SUBCASE("compiler.openOutput fails when nothing was compiled") {
        const auto response = nlohmann::json::parse(dispatcher->dispatch(
            R"([{"version":1,"id":"15","method":"compiler.openOutput","params":{}}])"));
        CHECK(response["ok"] == false);
        CHECK(response["error"]["code"] == "open_failed");
    }
}

TEST_CASE("async compiler methods: start, status, cancel") {
    Fixture fx;
    auto dispatcher = fx.dispatcher();

    const auto started = nlohmann::json::parse(dispatcher->dispatch(
        R"([{"version":1,"id":"7","method":"compiler.start",)"
        R"("params":{"projectPath":"/tmp/project","mainFile":"main.tex"}}])"));
    CHECK(started["ok"] == true);
    CHECK(started["result"]["started"] == true);

    const auto status = nlohmann::json::parse(dispatcher->dispatch(
        R"([{"version":1,"id":"8","method":"compiler.status","params":{}}])"));
    CHECK(status["ok"] == true);
    CHECK(status["result"]["state"].get<std::string>().empty() == false);
    CHECK(status["result"]["progress"].is_number_integer());
    CHECK(status["result"]["failedStageKey"].is_string());
    CHECK(status["result"]["stageDurationsMs"].is_object());
    CHECK(status["result"]["logTail"].is_string());
    CHECK(status["result"]["notice"].is_string());

    const auto canceled = nlohmann::json::parse(dispatcher->dispatch(
        R"([{"version":1,"id":"9","method":"compiler.cancel","params":{}}])"));
    CHECK(canceled["ok"] == true);
    CHECK(canceled["result"].contains("canceled"));
}

TEST_CASE("project.scan surfaces the remembered main file") {
    Fixture fx;
    const auto dispatcher = fx.dispatcher();

    SUBCASE("returns the stored choice for the scanned project") {
        fx.recents->entries = {{"/tmp/project", 1'000, true, "otros/principal.tex"}};
        const auto response = nlohmann::json::parse(dispatcher->dispatch(
            R"([{"version":1,"id":"30","method":"project.scan",)"
            R"("params":{"projectPath":"/tmp/project"}}])"));
        CHECK(response["ok"] == true);
        CHECK(response["result"]["savedMainFile"] == "otros/principal.tex");
    }

    SUBCASE("returns empty when nothing was remembered") {
        const auto response = nlohmann::json::parse(dispatcher->dispatch(
            R"([{"version":1,"id":"31","method":"project.scan",)"
            R"("params":{"projectPath":"/tmp/project"}}])"));
        CHECK(response["ok"] == true);
        CHECK(response["result"]["savedMainFile"] == "");
    }

    SUBCASE("routes project.rememberMainFile through the service") {
        fx.recents->entries = {{"/tmp/project", 1'000, true, ""}};
        const auto response = nlohmann::json::parse(dispatcher->dispatch(
            R"([{"version":1,"id":"32","method":"project.rememberMainFile",)"
            R"("params":{"projectPath":"/tmp/project","mainFile":"nuevo.tex"}}])"));
        CHECK(response["ok"] == true);
        CHECK(fx.recents->remember_calls == 1);
        CHECK(fx.recents->last_remembered.first == "/tmp/project");
        CHECK(fx.recents->last_remembered.second == "nuevo.tex");
        CHECK(fx.recents->entries[0].main_file == "nuevo.tex");
    }
}

TEST_CASE("recent project methods list, record and remove") {
    Fixture fx;
    auto dispatcher = fx.dispatcher();

    SUBCASE("lists persisted recents") {
        fx.recents->entries = {{"/tmp/proj-a", 1'000}, {"/tmp/proj-b", 500}};
        const auto response = nlohmann::json::parse(dispatcher->dispatch(
            R"([{"version":1,"id":"20","method":"project.recentProjects","params":{}}])"));
        CHECK(response["ok"] == true);
        CHECK(response["result"]["projects"].size() == 2);
        CHECK(response["result"]["projects"][0]["path"] == "/tmp/proj-a");
        CHECK(response["result"]["projects"][0]["lastOpenedMs"] == 1'000);
    }

    SUBCASE("records a path") {
        const auto response = nlohmann::json::parse(dispatcher->dispatch(
            R"([{"version":1,"id":"21","method":"project.recordRecent",)"
            R"("params":{"path":"/tmp/proj-x"}}])"));
        CHECK(response["ok"] == true);
        CHECK(fx.recents->record_calls == 1);
        CHECK(fx.recents->last_recorded == "/tmp/proj-x");
    }

    SUBCASE("purges missing projects and reports the count") {
        fx.recents->entries = {{"/tmp/gone-a", 1'000, false}, {"/tmp/live", 900, true}, {"/tmp/gone-b", 800, false}};
        const auto response = nlohmann::json::parse(dispatcher->dispatch(
            R"([{"version":1,"id":"24","method":"project.removeMissingRecents","params":{}}])"));
        CHECK(response["ok"] == true);
        CHECK(response["result"]["removed"] == 2);
        CHECK(fx.recents->purge_calls == 1);
    }

    SUBCASE("removes a path and reports misses") {
        fx.recents->entries = {{"/tmp/proj-a", 1'000}};
        const auto removed = nlohmann::json::parse(dispatcher->dispatch(
            R"([{"version":1,"id":"22","method":"project.removeRecent",)"
            R"("params":{"path":"/tmp/proj-a"}}])"));
        CHECK(removed["result"]["removed"] == true);

        const auto missing = nlohmann::json::parse(dispatcher->dispatch(
            R"([{"version":1,"id":"23","method":"project.removeRecent",)"
            R"("params":{"path":"/tmp/ghost"}}])"));
        CHECK(missing["result"]["removed"] == false);
    }
}

TEST_CASE("dispatcher returns failure responses for failed operations") {
    Fixture fx;
    fx.compiler->fail = true;
    const auto response = nlohmann::json::parse(fx.dispatcher()->dispatch(
        R"([{"version":1,"id":"6","method":"compiler.compile",)"
        R"("params":{"projectPath":"/tmp/project","mainFile":"main.tex"}}])"));

    CHECK(response["ok"] == false);
    CHECK(response["error"]["message"] == "boom");
}

TEST_CASE("dispatcher editor.presets reports per-preset install status") {
    Fixture fx;
    const auto response = nlohmann::json::parse(fx.dispatcher()->dispatch(
        R"([{"version":1,"id":"30","method":"editor.presets","params":{}}])"));

    CHECK(response["ok"] == true);
    CHECK(response["result"]["availability"] == true);

    const auto& presets = response["result"]["presets"];
    REQUIRE(presets.is_array());
    bool saw_installed = false;
    bool saw_missing = false;
    for (const auto& preset : presets) {
        REQUIRE(preset.contains("installed"));
        if (preset["installed"].get<bool>()) {
            saw_installed = true;
        } else {
            saw_missing = true;
        }
    }
    CHECK(saw_installed);
    CHECK(saw_missing);
}

TEST_CASE("project.scan returns style files in their own section") {
    Fixture fx;
    fx.scanner->info = {};
    fx.scanner->info.projectPath = "/tmp/project";
    fx.scanner->info.texFiles = {"main.tex"};
    fx.scanner->info.mainFileCandidate = "main.tex";
    fx.scanner->info.chapters = {{"main.tex", "", 100}};
    fx.scanner->info.diagrams = {{"flujo.mmd", "diagramas", 200}};
    fx.scanner->info.styles = {{"thesis.sty", "", 300}, {"references.bib", "biblio", 400}};
    fx.scanner->info.assets = {{"logo.png", "img", 500}};
    const auto dispatcher = fx.dispatcher();

    const auto response = nlohmann::json::parse(dispatcher->dispatch(
        R"([{"version":1,"id":"40","method":"project.scan",)"
        R"("params":{"projectPath":"/tmp/project"}}])"));

    REQUIRE(response["ok"] == true);
    const auto& styles = response["result"]["styles"];
    REQUIRE(styles.is_array());
    REQUIRE(styles.size() == 2);
    CHECK(styles[0]["name"] == "thesis.sty");
    CHECK(styles[0]["folder"] == "");
    CHECK(styles[1]["name"] == "references.bib");
    CHECK(styles[1]["folder"] == "biblio");
    CHECK(response["result"]["diagrams"].size() == 1);
    CHECK(response["result"]["assets"].size() == 1);
}

TEST_CASE("SystemProjectScanner classifies .sty/.cls/.bib/.cfg/.def as styles") {
    const auto temp_root = std::filesystem::temp_directory_path() /
                           ("latex-scanner-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temp_root / "estilos");
    std::filesystem::create_directories(temp_root / "img");
    const auto make_file = [](const std::filesystem::path& path, const std::string& content) {
        std::ofstream out(path, std::ios::binary);
        out << content;
    };
    make_file(temp_root / "main.tex", "\\documentclass{article}\\n\\begin{document}x\\end{document}\n");
    make_file(temp_root / "thesis.sty", "\\ProvidesPackage{thesis}\n");
    make_file(temp_root / "estilos" / "cover.cls", "\\ProvidesClass{cover}\n");
    make_file(temp_root / "estilos" / "references.bib", "@article{k, title={t}}\n");
    make_file(temp_root / "estilos" / "layout.cfg", "\\ExecuteOptions{}\n");
    make_file(temp_root / "estilos" / "symbols.def", "\\newcommand{\\sym}{x}\n");
    make_file(temp_root / "img" / "logo.png", "png");

    adapters::filesystem::SystemProjectScanner scanner;
    const auto info = scanner.scan(temp_root.generic_string());

    CHECK(info.styles.size() == 5);
    CHECK(info.chapters.size() == 1);
    CHECK(info.assets.size() == 1);
    bool saw_root_sty = false;
    bool saw_subfolder_bib = false;
    for (const auto& style : info.styles) {
        if (style.name == "thesis.sty") {
            CHECK(style.folder == "");
            saw_root_sty = true;
        }
        if (style.name == "references.bib") {
            CHECK(style.folder == "estilos");
            saw_subfolder_bib = true;
        }
    }
    CHECK(saw_root_sty);
    CHECK(saw_subfolder_bib);

    std::error_code cleanup_ec;
    std::filesystem::remove_all(temp_root, cleanup_ec);
}

TEST_CASE("SystemProjectScanner skips dependency, build and config folders") {
    const auto temp_root = std::filesystem::temp_directory_path() /
                           ("latex-scanner-exclude-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temp_root / "node_modules");
    std::filesystem::create_directories(temp_root / "Build");
    std::filesystem::create_directories(temp_root / "target");
    std::filesystem::create_directories(temp_root / ".git");
    std::filesystem::create_directories(temp_root / "secciones");
    const auto make_file = [](const std::filesystem::path& path, const std::string& content) {
        std::ofstream out(path, std::ios::binary);
        out << content;
    };
    make_file(temp_root / "main.tex", "\\documentclass{article}\n");
    make_file(temp_root / "secciones" / "intro.tex", "Capitulo.\n");
    make_file(temp_root / "node_modules" / "fake.sty", "\\ProvidesPackage{fake}\n");
    make_file(temp_root / "node_modules" / "pkg.mmd", "graph TD\n");
    make_file(temp_root / "Build" / "out.tex", "no debe aparecer\n");
    make_file(temp_root / "target" / "cache.png", "png");
    make_file(temp_root / ".git" / "config.tex", "no debe aparecer\n");

    adapters::filesystem::SystemProjectScanner scanner;
    const auto info = scanner.scan(temp_root.generic_string());

    CHECK(info.texFiles.size() == 2);
    CHECK(info.chapters.size() == 2);
    CHECK(info.styles.empty());
    CHECK(info.diagrams.empty());
    CHECK(info.assets.empty());

    std::error_code cleanup_ec;
    std::filesystem::remove_all(temp_root, cleanup_ec);
}


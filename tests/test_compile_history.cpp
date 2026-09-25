#include "adapters/history/JsonCompileHistory.hpp"
#include "core/CompileService.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

class FakeCompiler : public core::contracts::ICompiler {
public:
    std::vector<core::contracts::EngineInfo> availableEngines() override {
        return {{"fake", "Fake", true}};
    }

    core::contracts::CompileResult compile(const core::contracts::CompileRequest& request) override {
        return compile(request, nullptr);
    }

    core::contracts::CompileResult compile(const core::contracts::CompileRequest& request,
                                           core::contracts::ICompileProgress* progress) override {
        for (const auto& sample : progress_samples) {
            if (progress != nullptr) {
                core::contracts::ProgressInfo info;
                info.percent = sample.percent;
                info.stage = sample.stage;
                info.stage_key = sample.stage_key;
                info.file = sample.file;
                progress->onProgress(info);
            }
        }
        std::this_thread::sleep_for(hold_ms);
        return next_result;
    }

    void cancel(core::contracts::CompileId) override {}

    struct Sample {
        int percent;
        std::string stage;
        std::string stage_key;
        std::string file;
    };

    core::contracts::CompileResult next_result{true, "/tmp/out.pdf", {}};
    std::vector<Sample> progress_samples;
    std::chrono::milliseconds hold_ms{0};
};

class FakeFileStore : public core::contracts::IFileStore {
public:
    std::string readFile(const std::string& path) override {
        return files.at(path);
    }

    void writeFile(const std::string& path, const std::string& content) override {
        files[path] = content;
    }

    bool exists(const std::string& path) const override {
        return files.count(path) > 0;
    }

    std::vector<std::string> listDirectory(const std::string&) const override { return {}; }

    std::map<std::string, std::string> files;
};

class FakeEvents : public core::contracts::IEventPublisher {
public:
    void publish(const core::contracts::Event&) override {}
};

using State = core::CompileService::JobState;

bool wait_for_state(const core::CompileService& service, State expected, int timeout_ms = 3000) {
    for (int waited = 0; waited < timeout_ms; waited += 20) {
        if (service.jobStatus().state == expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return service.jobStatus().state == expected;
}

} // namespace

TEST_CASE("CompileService records a succeeded sync compile in history") {
    auto compiler = std::make_shared<FakeCompiler>();
    auto files = std::make_shared<FakeFileStore>();
    auto events = std::make_shared<FakeEvents>();
    auto history = std::make_shared<adapters::history::JsonCompileHistory>(files, "/tmp/lc-history-test");
    core::CompileService service(compiler, files, events, nullptr, history);

    compiler->next_result = {true, "/tmp/project/main.pdf", {}};
    service.compileProject({"/tmp/project", "main.tex"});

    const auto entries = service.history("/tmp/project");
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].result == "succeeded");
    CHECK(entries[0].main_file == "main.tex");
    CHECK(entries[0].output_path == "/tmp/project/main.pdf");
    CHECK(entries[0].duration_ms >= 0);
    CHECK(entries[0].timestamp_ms > 0);
}

TEST_CASE("CompileService records failures with the error message") {
    auto compiler = std::make_shared<FakeCompiler>();
    auto files = std::make_shared<FakeFileStore>();
    auto events = std::make_shared<FakeEvents>();
    auto history = std::make_shared<adapters::history::JsonCompileHistory>(files, "/tmp/lc-history-test");
    core::CompileService service(compiler, files, events, nullptr, history);

    compiler->next_result = {false, {}, "tectonic exploded"};
    service.compileProject({"/tmp/project", "main.tex"});

    const auto entries = service.history("/tmp/project");
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].result == "failed");
    CHECK(entries[0].error_message == "tectonic exploded");
    CHECK(entries[0].output_path.empty());
}

TEST_CASE("CompileService records canceled async jobs in history") {
    auto compiler = std::make_shared<FakeCompiler>();
    auto files = std::make_shared<FakeFileStore>();
    auto events = std::make_shared<FakeEvents>();
    auto history = std::make_shared<adapters::history::JsonCompileHistory>(files, "/tmp/lc-history-test");
    core::CompileService service(compiler, files, events, nullptr, history);

    compiler->next_result = {false, {}, "Compilation canceled."};
    CHECK(service.startCompile({"/tmp/other-project", "tesis.tex"}));
    CHECK(wait_for_state(service, State::Canceled));

    const auto entries = service.history("/tmp/other-project");
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].result == "canceled");
    CHECK(entries[0].main_file == "tesis.tex");
}

TEST_CASE("Async history entry stores reached stages so failures can be compared per phase") {
    auto compiler = std::make_shared<FakeCompiler>();
    auto files = std::make_shared<FakeFileStore>();
    auto events = std::make_shared<FakeEvents>();
    auto history = std::make_shared<adapters::history::JsonCompileHistory>(files, "/tmp/lc-history-test");
    core::CompileService service(compiler, files, events, nullptr, history);

    compiler->progress_samples = {
        {15, "Compilando TeX", "tex", ""},
        {85, "Ensamblado del PDF", "assemble", ""},
    };
    compiler->next_result = {false, {}, "boom"};
    CHECK(service.startCompile({"/tmp/patterns-project", "main.tex"}));
    CHECK(wait_for_state(service, State::Failed));

    const auto entries = service.history("/tmp/patterns-project");
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].stages_reached == std::vector<std::string>{"tex", "assemble"});

    // Success without progress keeps an empty trail: reached stages are
    // per-job state and must not leak from the previous failed run.
    compiler->progress_samples.clear();
    compiler->next_result = {true, "/tmp/out.pdf", {}};
    CHECK(service.startCompile({"/tmp/patterns-project", "main.tex"}));
    CHECK(wait_for_state(service, State::Succeeded));

    const auto after = service.history("/tmp/patterns-project");
    REQUIRE(after.size() == 2);
    CHECK(after.front().result == "succeeded");
    CHECK(after.front().stages_reached.empty());
    CHECK(after[1].stages_reached == std::vector<std::string>{"tex", "assemble"});
}

TEST_CASE("CompileService works without a history store (backward compatible)") {
    auto compiler = std::make_shared<FakeCompiler>();
    auto files = std::make_shared<FakeFileStore>();
    auto events = std::make_shared<FakeEvents>();
    core::CompileService service(compiler, files, events);

    service.compileProject({"/tmp/project", "main.tex"});
    CHECK(service.history("/tmp/project").empty());
}

TEST_CASE("JsonCompileHistory persists across instances and caps entries") {
    auto files = std::make_shared<FakeFileStore>();
    const std::string project = "/Users/me/Tesis-LaTeX";

    {
        adapters::history::JsonCompileHistory history(files, "/tmp/lc-history-test");
        for (int i = 0; i < 55; ++i) {
            core::contracts::HistoryEntry entry;
            entry.timestamp_ms = 1'000 + i;
            entry.duration_ms = 100 + i;
            entry.result = i % 2 == 0 ? "succeeded" : "failed";
            entry.main_file = "main.tex";
            history.record(project, entry);
        }
    }

    // A fresh instance reads from the same file store: persistence works.
    adapters::history::JsonCompileHistory reloaded(files, "/tmp/lc-history-test");
    const auto entries = reloaded.entries(project);
    REQUIRE(entries.size() == adapters::history::JsonCompileHistory::kMaxEntriesPerProject);

    // Newest first and exactly the most recent 50 kept.
    CHECK(entries.front().timestamp_ms == 1'054);
    CHECK(entries.back().timestamp_ms == 1'005);

    const auto missing = reloaded.entries("/Users/me/OtherProject");
    CHECK(missing.empty());
}

TEST_CASE("JsonCompileHistory file content is readable JSON") {
    auto files = std::make_shared<FakeFileStore>();
    adapters::history::JsonCompileHistory history(files, "/tmp/lc-history-test");

    core::contracts::HistoryEntry entry;
    entry.timestamp_ms = 1'700'000'000'123;
    entry.duration_ms = 4'321;
    entry.result = "succeeded";
    entry.main_file = "tesis.tex";
    entry.output_path = "/tmp/p/tesis.pdf";
    entry.error_message = "quote\"back\\slash";
    entry.stages_reached = {"tex", "assemble"};
    entry.stage_durations_ms = {{"tex", 4'000}, {"assemble", 321}};
    history.record("/tmp/proj", entry);

    const auto document = nlohmann::json::parse(files->files.begin()->second);
    REQUIRE(document.is_array());
    REQUIRE(document.size() == 1);
    CHECK(document[0]["result"] == "succeeded");
    CHECK(document[0]["durationMs"] == 4'321);
    CHECK(document[0]["errorMessage"] == "quote\"back\\slash");
    CHECK(document[0]["stagesReached"] == nlohmann::json::array({"tex", "assemble"}));
    CHECK(document[0]["stageDurationsMs"] == nlohmann::json{{"tex", 4'000}, {"assemble", 321}});
}

TEST_CASE("JsonCompileHistory tolerates a corrupt history file") {
    auto files = std::make_shared<FakeFileStore>();
    const std::string project = "/tmp/proj";

    core::contracts::HistoryEntry good;
    good.timestamp_ms = 500;
    good.result = "succeeded";
    adapters::history::JsonCompileHistory writer(files, "/tmp/lc-history-test");
    writer.record(project, good);

    // Corrupt the stored JSON directly.
    for (auto& [path, content] : files->files) {
        content = "{not json at all";
    }

    adapters::history::JsonCompileHistory reader(files, "/tmp/lc-history-test");
    CHECK(reader.entries(project).empty());

    // Recording after corruption rebuilds a valid file from scratch.
    core::contracts::HistoryEntry next;
    next.timestamp_ms = 900;
    next.result = "failed";
    next.error_message = "boom";
    reader.record(project, next);

    const auto entries = reader.entries(project);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].result == "failed");
}

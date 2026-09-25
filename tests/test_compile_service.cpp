#include "core/CompileService.hpp"
#include "core/contracts/INotifier.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>
#include <atomic>
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
        ++compile_calls;
        last_request = request;
        for (const auto& chunk : stream_chunks) {
            if (progress != nullptr) {
                progress->onOutput(chunk);
            }
        }
        for (const auto& sample : progress_samples) {
            if (progress != nullptr) {
                std::this_thread::sleep_for(sample_delay_ms);
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

    void cancel(core::contracts::CompileId) override {
        canceled = true;
    }

    struct Sample {
        int percent;
        std::string stage;
        std::string stage_key;
        std::string file;
    };

    core::contracts::CompileRequest last_request;
    core::contracts::CompileResult next_result{true, "/tmp/out.pdf", {}};
    std::vector<std::string> stream_chunks;
    std::vector<Sample> progress_samples;
    std::chrono::milliseconds hold_ms{0};
    std::chrono::milliseconds sample_delay_ms{0};
    std::atomic<int> compile_calls{0};
    std::atomic<bool> canceled{false};
};

class FakeFileStore : public core::contracts::IFileStore {
public:
    std::string readFile(const std::string&) override { return {}; }
    void writeFile(const std::string&, const std::string&) override {}
    bool exists(const std::string&) const override { return true; }
    std::vector<std::string> listDirectory(const std::string&) const override { return {}; }
};

class FakeEvents : public core::contracts::IEventPublisher {
public:
    void publish(const core::contracts::Event& event) override {
        topics.push_back(event.topic);
        payloads.push_back(event.payload);
    }
    std::vector<std::string> topics;
    std::vector<std::string> payloads;
};

using State = core::CompileService::JobState;

bool wait_for_state(const core::CompileService& service, State expected, int timeout_ms = 10000) {
    for (int waited = 0; waited < timeout_ms; waited += 20) {
        if (service.jobStatus().state == expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return service.jobStatus().state == expected;
}

} // namespace

TEST_CASE("CompileService turns compiler progress into JSON-safe compile_progress events") {
    auto compiler = std::make_shared<FakeCompiler>();
    auto files = std::make_shared<FakeFileStore>();
    auto events = std::make_shared<FakeEvents>();
    core::CompileService service(compiler, files, events);

    compiler->progress_samples = {
        {15, "Compilando TeX", "tex", "capítulos/intro.tex"},
        {85, "Ensamblado del PDF", "assemble", ""},
    };
    compiler->next_result = {true, "/tmp/out.pdf", {}};

    CHECK(service.startCompile({"/tmp/project", "main.tex"}));
    CHECK(wait_for_state(service, State::Succeeded));

    std::string last_payload;
    for (std::size_t i = 0; i < events->topics.size(); ++i) {
        if (events->topics[i] == "compile_progress") {
            last_payload = events->payloads[i];
        }
    }
    REQUIRE(last_payload.empty() == false);

    const auto parsed = nlohmann::json::parse(last_payload);
    CHECK(parsed["progress"] == 85);
    CHECK(parsed["stage"] == "Ensamblado del PDF");
    CHECK(parsed["stageKey"] == "assemble");
    CHECK(parsed["stagesReached"] == std::vector<std::string>{"tex", "assemble"});

    // The first sample's UTF-8 file name survived the JSON round-trip.
    bool saw_utf8_file = false;
    for (std::size_t i = 0; i < events->topics.size(); ++i) {
        if (events->topics[i] != "compile_progress") {
            continue;
        }
        const auto payload = nlohmann::json::parse(events->payloads[i]);
        saw_utf8_file = saw_utf8_file || payload["file"].get<std::string>() == "capítulos/intro.tex";
    }
    CHECK(saw_utf8_file);

    // Completion lifts the percentage to 100, but stage/file context stays.
    const auto status = service.jobStatus();
    CHECK(status.progress_percent == 100);
    CHECK(status.stage == "Ensamblado del PDF");
    CHECK(status.stage_key == "assemble");
    CHECK(status.current_file == "capítulos/intro.tex");
    CHECK(status.stages_reached.size() == 2);
    CHECK(status.stages_reached[0] == "tex");
    CHECK(status.stages_reached[1] == "assemble");
}

TEST_CASE("CompileService delegates to the ICompiler contract") {
    auto compiler = std::make_shared<FakeCompiler>();
    auto files = std::make_shared<FakeFileStore>();
    auto events = std::make_shared<FakeEvents>();
    core::CompileService service(compiler, files, events);

    const core::contracts::CompileRequest request{"/tmp/project", "main.tex"};
    const auto result = service.compileProject(request);

    CHECK(compiler->compile_calls == 1);
    CHECK(compiler->last_request.mainFile == "main.tex");
    CHECK(result.success);
}

TEST_CASE("CompileService publishes success and failure events (sync path)") {
    auto compiler = std::make_shared<FakeCompiler>();
    auto files = std::make_shared<FakeFileStore>();
    auto events = std::make_shared<FakeEvents>();
    core::CompileService service(compiler, files, events);

    compiler->next_result = {true, "/tmp/out.pdf", {}};
    service.compileProject({"/tmp/project", "main.tex"});
    REQUIRE(events->topics.size() == 2);
    CHECK(events->topics[0] == "compile_started");
    CHECK(events->topics[1] == "compile_success");

    events->topics.clear();
    compiler->next_result = {false, {}, "tectonic failed"};
    const auto failed = service.compileProject({"/tmp/project", "main.tex"});
    CHECK(!failed.success);
    REQUIRE(events->topics.size() == 2);
    CHECK(events->topics[1] == "compile_error");
}

TEST_CASE("Async job runs to success with streamed log") {
    auto compiler = std::make_shared<FakeCompiler>();
    auto files = std::make_shared<FakeFileStore>();
    auto events = std::make_shared<FakeEvents>();
    core::CompileService service(compiler, files, events);

    compiler->stream_chunks = {"note: running\n", "xdvipdfmx...\n"};
    compiler->next_result = {true, "/tmp/out.pdf", {}};

    CHECK(service.startCompile({"/tmp/project", "main.tex"}));
    CHECK(service.jobStatus().state == State::Running);
    CHECK(wait_for_state(service, State::Succeeded));

    const auto status = service.jobStatus();
    CHECK(status.progress_percent == 100);
    CHECK(status.output_path == "/tmp/out.pdf");
    CHECK(status.log_tail.find("xdvipdfmx") != std::string::npos);
}

TEST_CASE("Async job reports failures and enforces a single running job") {
    auto compiler = std::make_shared<FakeCompiler>();
    auto files = std::make_shared<FakeFileStore>();
    auto events = std::make_shared<FakeEvents>();
    core::CompileService service(compiler, files, events);

    compiler->hold_ms = std::chrono::milliseconds(150);
    compiler->next_result = {false, {}, "boom"};

    CHECK(service.startCompile({"/tmp/project", "main.tex"}));
    CHECK_FALSE(service.startCompile({"/tmp/project", "other.tex"}));
    CHECK(wait_for_state(service, State::Failed));
    CHECK(service.jobStatus().message == "boom");
}

TEST_CASE("Stage durations are measured between stage transitions and frozen on completion") {
    auto compiler = std::make_shared<FakeCompiler>();
    auto files = std::make_shared<FakeFileStore>();
    auto events = std::make_shared<FakeEvents>();
    core::CompileService service(compiler, files, events);

    compiler->hold_ms = std::chrono::milliseconds(120);
    compiler->sample_delay_ms = std::chrono::milliseconds(60);
    compiler->progress_samples = {
        {15, "Compilando TeX", "tex", ""},
        {85, "Ensamblado del PDF", "assemble", ""},
    };
    compiler->next_result = {true, "/tmp/out.pdf", {}};

    CHECK(service.startCompile({"/tmp/project", "main.tex"}));
    CHECK(wait_for_state(service, State::Succeeded));

    const auto status = service.jobStatus();
    // Each sample fires after its delay, so tex spans sample1->sample2 (60ms)
    // and assemble spans sample2->finish (the 120ms hold).
    CHECK(status.stage_durations_ms.at("tex") >= 40);
    CHECK(status.stage_durations_ms.at("assemble") >= 100);
    CHECK(status.stage_durations_ms.size() == 2);

    // Durations are frozen after the job ends: status snapshots never grow.
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    const auto later = service.jobStatus();
    CHECK(later.stage_durations_ms == status.stage_durations_ms);
}

TEST_CASE("Failure keeps measured stage durations without closing the failure stage") {
    auto compiler = std::make_shared<FakeCompiler>();
    auto files = std::make_shared<FakeFileStore>();
    auto events = std::make_shared<FakeEvents>();
    core::CompileService service(compiler, files, events);

    compiler->hold_ms = std::chrono::milliseconds(100);
    compiler->sample_delay_ms = std::chrono::milliseconds(50);
    compiler->progress_samples = {
        {15, "Compilando TeX", "tex", ""},
        {85, "Ensamblado del PDF", "assemble", ""},
    };
    compiler->next_result = {false, {}, "boom"};

    CHECK(service.startCompile({"/tmp/project", "main.tex"}));
    CHECK(wait_for_state(service, State::Failed));

    const auto status = service.jobStatus();
    CHECK(status.stage_durations_ms.at("tex") >= 40);
    CHECK(status.stage_durations_ms.at("assemble") >= 90);
    CHECK(status.stage_durations_ms.size() == 2);
    CHECK(service.jobStatus().stage_durations_ms == status.stage_durations_ms);
}

TEST_CASE("Failure records the stage where the compile broke and clears it on the next job") {
    auto compiler = std::make_shared<FakeCompiler>();
    auto files = std::make_shared<FakeFileStore>();
    auto events = std::make_shared<FakeEvents>();
    core::CompileService service(compiler, files, events);

    compiler->hold_ms = std::chrono::milliseconds(30);
    compiler->progress_samples = {
        {15, "Compilando TeX", "tex", "capítulos/intro.tex"},
        {85, "Ensamblado del PDF", "assemble", ""},
    };
    compiler->next_result = {false, {}, "capítulos/intro.tex:12: Undefined control sequence"};

    CHECK(service.startCompile({"/tmp/project", "main.tex"}));
    CHECK(wait_for_state(service, State::Failed));

    const auto failed = service.jobStatus();
    CHECK(failed.failed_stage_key == "assemble");
    CHECK(failed.stages_reached == std::vector<std::string>{"tex", "assemble"});

    bool saw_stage_key_in_error = false;
    for (std::size_t i = 0; i < events->topics.size(); ++i) {
        if (events->topics[i] != "compile_error") {
            continue;
        }
        const auto payload = nlohmann::json::parse(events->payloads[i]);
        saw_stage_key_in_error = saw_stage_key_in_error || payload["stageKey"].get<std::string>() == "assemble";
    }
    CHECK(saw_stage_key_in_error);

    // A subsequent success clears the failed stage so stale red chips never linger.
    compiler->progress_samples.clear();
    compiler->next_result = {true, "/tmp/out.pdf", {}};
    CHECK(service.startCompile({"/tmp/project", "main.tex"}));
    CHECK(wait_for_state(service, State::Succeeded));
    CHECK(service.jobStatus().failed_stage_key.empty());
}

TEST_CASE("Async job emits JSON-safe compile_output events per line") {
    auto compiler = std::make_shared<FakeCompiler>();
    auto files = std::make_shared<FakeFileStore>();
    auto events = std::make_shared<FakeEvents>();
    core::CompileService service(compiler, files, events);

    compiler->stream_chunks = {
        "note: Running xdvipdfmx\n",
        "quote\"slash\\ tab\tnext\n",
        "partial line without newline",
    };
    compiler->next_result = {true, "/tmp/out.pdf", {}};

    CHECK(service.startCompile({"/tmp/project", "main.tex"}));
    CHECK(wait_for_state(service, State::Succeeded));

    int output_events = 0;
    for (const auto& topic : events->topics) {
        if (topic == "compile_output") {
            ++output_events;
        }
    }
    CHECK(output_events >= 2);

    // Validate the last emitted payload parses as JSON with a line field.
    bool found_render_line = false;
    for (std::size_t i = 0; i + 1 < events->topics.size(); ++i) {
        if (events->topics[i] != "compile_output") {
            continue;
        }
        const auto payload = events->payloads[i];
        if (payload.find("\\u") != std::string::npos) {
            continue;
        }
        try {
            const auto parsed = nlohmann::json::parse(payload);
            if (parsed.contains("line")) {
                found_render_line = found_render_line ||
                    parsed["line"].get<std::string>().find("xdvipdfmx") != std::string::npos;
            }
        } catch (...) {
            MESSAGE("compile_output payload is not valid JSON: " << payload);
            REQUIRE(false);
        }
    }
    CHECK(found_render_line);

    // A trailing partial line stays buffered in the service, never in the log.
    const auto status = service.jobStatus();
    CHECK(status.log_tail.find("partial line without newline") == std::string::npos);
}

class RecordingNotifier : public core::contracts::INotifier {
public:
    void notify(const std::string& title, const std::string& body, const std::string& open_path) override {
        calls.push_back({title, body, open_path});
    }

    struct Call {
        std::string title;
        std::string body;
        std::string open_path;
    };
    std::vector<Call> calls;
};

TEST_CASE("Async compile notifies the OS on success and failure, not on cancel") {
    auto compiler = std::make_shared<FakeCompiler>();
    auto events = std::make_shared<FakeEvents>();
    core::CompileService service(compiler, std::make_shared<FakeFileStore>(), events);
    auto notifier = std::make_shared<RecordingNotifier>();
    service.setNotifier(notifier);

    compiler->next_result = {true, "/tmp/proj/AXON.pdf", {}};
    CHECK(service.startCompile({"/tmp/project", "main.tex"}));
    CHECK(wait_for_state(service, State::Succeeded));
    REQUIRE(notifier->calls.size() == 1);
    CHECK(notifier->calls[0].title == "Compile finished");
    CHECK(notifier->calls[0].body.find("AXON.pdf") != std::string::npos);
    // The notification must carry the PDF so a click can open it.
    CHECK(notifier->calls[0].open_path == "/tmp/proj/AXON.pdf");

    notifier->calls.clear();
    compiler->next_result = {false, {}, "./main.tex:12: Undefined control sequence."};
    CHECK(service.startCompile({"/tmp/project", "main.tex"}));
    CHECK(wait_for_state(service, State::Failed));
    REQUIRE(notifier->calls.size() == 1);
    CHECK(notifier->calls[0].title == "Compile failed");
    CHECK(notifier->calls[0].body.find("Undefined control sequence") != std::string::npos);
    // Failures carry no file to open.
    CHECK(notifier->calls[0].open_path.empty());

    // The user canceled it themselves: a notification would be noise.
    notifier->calls.clear();
    compiler->next_result = {false, {}, "Compilation canceled."};
    CHECK(service.startCompile({"/tmp/project", "main.tex"}));
    CHECK(wait_for_state(service, State::Canceled));
    CHECK(notifier->calls.empty());
}

TEST_CASE("Notification preference gate silences OS notifications at runtime") {
    auto compiler = std::make_shared<FakeCompiler>();
    auto events = std::make_shared<FakeEvents>();
    core::CompileService service(compiler, std::make_shared<FakeFileStore>(), events);
    auto notifier = std::make_shared<RecordingNotifier>();
    service.setNotifier(notifier);
    service.setNotificationsEnabled(false);

    compiler->next_result = {true, "/tmp/out.pdf", {}};
    CHECK(service.startCompile({"/tmp/project", "main.tex"}));
    CHECK(wait_for_state(service, State::Succeeded));
    CHECK(notifier->calls.empty());

    // Re-enabling applies to later runs without restarting the app.
    service.setNotificationsEnabled(true);
    CHECK(service.startCompile({"/tmp/project", "main.tex"}));
    CHECK(wait_for_state(service, State::Succeeded));
    CHECK(notifier->calls.size() == 1);
}

TEST_CASE("Sync compile path never notifies the OS") {
    auto compiler = std::make_shared<FakeCompiler>();
    auto events = std::make_shared<FakeEvents>();
    core::CompileService service(compiler, std::make_shared<FakeFileStore>(), events);
    auto notifier = std::make_shared<RecordingNotifier>();
    service.setNotifier(notifier);

    compiler->next_result = {true, "/tmp/out.pdf", {}};
    const auto result = service.compileProject({"/tmp/project", "main.tex"});
    CHECK(result.success);
    CHECK(notifier->calls.empty());
}

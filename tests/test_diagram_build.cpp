#include "core/CompileService.hpp"
#include "core/DiagramBuildService.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <cstdlib>

namespace {

namespace fs = std::filesystem;

std::string readByPath(const fs::path& path) {
    std::ifstream in(path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return content;
}

using core::contracts::IProcessRunner;
using core::contracts::OutputSink;
using core::contracts::ProcessRequest;
using core::contracts::ProcessResult;

// Records every spawned command and can create the expected artifact, as the
// real toolchain does, so freshness caching is exercised end to end.
class FakeRunner : public IProcessRunner {
public:
    ProcessResult run(const ProcessRequest& request) override {
        return run(request, {});
    }

    ProcessResult run(const ProcessRequest& request, const OutputSink& sink) override {
        ++run_count_;
        commands.push_back(request.command);
        args.push_back(request.args);

        // mmdc invocation: write the requested SVG next to the source and
        // snapshot the remapped input (the temp file is deleted afterwards).
        if (write_svg && request.args.size() >= 2 && request.args[0].find("mmdc") != std::string::npos) {
            for (std::size_t i = 0; i + 1 < request.args.size(); ++i) {
                if (request.args[i] == "-i") {
                    last_mmd_input = request.args[i + 1];
                    last_mmd_content = readByPath(last_mmd_input);
                }
                if (request.args[i] == "-o") {
                    std::ofstream out(request.args[i + 1], std::ios::trunc);
                    out << "<svg xmlns=\"http://www.w3.org/2000/svg\"><g fill=\"rgb(46,40,24)\"/></svg>";
                }
            }
        }
        if (request.command.find("rsvg") != std::string::npos && request.args.size() >= 2) {
            for (std::size_t i = 0; i + 1 < request.args.size(); ++i) {
                if (request.args[i] == "-o") {
                    std::ofstream out(request.args[i + 1], std::ios::trunc);
                    out << "%PDF-1.4 fake converted";
                }
            }
        }
        if (sink && !script_output.empty()) {
            sink(script_output.data(), script_output.size());
        }
        return {next_exit_code, script_output, "", false};
    }

    void cancelActive() override {}

    std::vector<std::string> commands;
    std::vector<std::vector<std::string>> args;
    std::string last_mmd_input;
    std::string last_mmd_content;
    std::string script_output;
    bool write_svg = true;
    int next_exit_code = 0;
    int run_count_ = 0;
};

struct TestProject {
    fs::path root;

    static TestProject make() {
        TestProject project;
        project.root = fs::temp_directory_path() /
                       ("diagram-build-" + std::to_string(++counter));
        fs::create_directories(project.root / "diagrams");
        return project;
    }

    void write(const fs::path& relative, const std::string& content) const {
        std::ofstream out(root / relative, std::ios::trunc);
        out << content;
    }

    std::string read(const fs::path& relative) const {
        std::ifstream in(root / relative);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return content;
    }

    ~TestProject() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    static inline int counter = 0;
};

void installFakeToolchain(const TestProject& project, FakeRunner& runner) {
    // The project-local toolchain: mmdc script resolved through node, plus
    // rsvg-convert. resolveTools looks for them inside node_modules/.bin first.
    fs::create_directories(project.root / "node_modules" / ".bin");
    const std::string base = (project.root / "node_modules" / ".bin").generic_string();
    runner.args.clear();
    // Tool resolution is filesystem based: create plain marker files.
    std::ofstream(base + "/mmdc", std::ios::trunc) << "#!/bin/sh\n";
    std::ofstream(base + "/rsvg-convert", std::ios::trunc) << "#!/bin/sh\n";
    std::ofstream(base + "/node", std::ios::trunc) << "#!/bin/sh\n";
}

// Minimal stubs so CompileService can run end to end in-process.
class StubStore : public core::contracts::IFileStore {
public:
    std::string readFile(const std::string&) override { return {}; }
    void writeFile(const std::string&, const std::string&) override {}
    bool exists(const std::string&) const override { return false; }
    std::vector<std::string> listDirectory(const std::string&) const override { return {}; }
};

class StubEvents : public core::contracts::IEventPublisher {
public:
    void publish(const core::contracts::Event&) override { ++count; }
    int count = 0;
};

class RecordingCompiler : public core::contracts::ICompiler {
public:
    std::vector<core::contracts::EngineInfo> availableEngines() override { return {}; }
    core::contracts::CompileResult compile(const core::contracts::CompileRequest&) override {
        compiled = true;
        return {true, "/tmp/out.pdf", {}};
    }
    void cancel(core::contracts::CompileId) override {}
    bool compiled = false;
};

} // namespace

TEST_CASE("CompileService builds diagrams before invoking the LaTeX engine") {
    const auto project = TestProject::make();
    project.write("diagrams/flow.mmd", "graph TD\n  A --> B\n");
    project.write("main.tex", "\\includegraphics{diagrams/flow.pdf}\n");

    auto runner = std::make_shared<FakeRunner>();
    installFakeToolchain(project, *runner);

    auto compiler = std::make_shared<RecordingCompiler>();
    core::CompileService service(compiler, std::make_shared<StubStore>(), std::make_shared<StubEvents>());
    service.setDiagramBuilder(std::make_shared<core::DiagramBuildService>(runner));

    core::contracts::CompileRequest request;
    request.projectPath = project.root.generic_string();
    request.mainFile = "main.tex";
    const auto result = service.compileProject(request);

    CHECK(result.success);
    CHECK(compiler->compiled);
    // The diagram toolchain ran before the engine was invoked.
    REQUIRE(runner->commands.size() >= 2);
    CHECK(runner->commands[0].find("node") != std::string::npos);
    CHECK(runner->commands[1].find("rsvg-convert") != std::string::npos);
    CHECK(fs::exists(project.root / "diagrams" / "flow.pdf"));

    // A second compile reuses the fresh artifacts (no tool runs at all).
    runner->commands.clear();
    const auto second = service.compileProject(request);
    CHECK(second.success);
    CHECK(runner->commands.empty());
}

TEST_CASE("buildAll renders demanded formats and remaps colors through node") {
    const auto project = TestProject::make();
    project.write("diagrams/hero.mmd", "graph TD\n  S[\"rgb(255, 245, 230)\"] --> E\n");
    project.write("diagrams/mermaid-dark-theme.json", "{}\n");
    project.write("diagrams/mermaid-dark.css", "svg { background: transparent; }\n");
    project.write("main.tex", "\\includegraphics{diagrams/hero.pdf}\n");

    auto runner = std::make_shared<FakeRunner>();
    installFakeToolchain(project, *runner);
    core::DiagramBuildService service(runner);

    const auto summary = service.buildAll(project.root.generic_string());
    CHECK(summary.rendered == 1);
    CHECK(summary.failed == 0);

    // First run: node mmdc ... ; second run: rsvg-convert ...
    REQUIRE(runner->commands.size() == 2);
    CHECK(runner->commands[0].find("node") != std::string::npos);
    const auto& mmdc_args = runner->args[0];
    REQUIRE(mmdc_args.size() >= 4);
    CHECK(mmdc_args[0].find("node_modules/.bin/mmdc") != std::string::npos);
    // Remapped source is passed to mmdc instead of the original .mmd.
    const std::string* input = nullptr;
    const std::string* output = nullptr;
    for (std::size_t i = 0; i + 1 < mmdc_args.size(); ++i) {
        if (mmdc_args[i] == "-i") input = &mmdc_args[i + 1];
        if (mmdc_args[i] == "-o") output = &mmdc_args[i + 1];
    }
    REQUIRE(input != nullptr);
    REQUIRE(output != nullptr);
    CHECK(input->find(".tmp.mmd") != std::string::npos);
    CHECK(runner->last_mmd_input == *input);
    CHECK(runner->last_mmd_content.find("rgb(46,40,24)") != std::string::npos);
    CHECK(runner->last_mmd_content.find("rgb(255, 245, 230)") == std::string::npos);

    const auto& rsvg_args = runner->args[1];
    REQUIRE(rsvg_args.size() >= 5);
    CHECK(rsvg_args[0] == "-f");
    CHECK(rsvg_args[1] == "pdf");
    CHECK(rsvg_args[3].find("hero.pdf") != std::string::npos);
    CHECK(runner->commands[1].find("rsvg-convert") != std::string::npos);
    CHECK(fs::exists(project.root / "diagrams" / "hero.pdf"));
}

TEST_CASE("buildAll skips up-to-date targets until the source changes") {
    const auto project = TestProject::make();
    project.write("diagrams/hero.mmd", "graph TD\n  A --> B\n");
    project.write("main.tex", "\\includegraphics{diagrams/hero.pdf}\n");

    auto runner = std::make_shared<FakeRunner>();
    installFakeToolchain(project, *runner);
    core::DiagramBuildService service(runner);

    auto first = service.buildAll(project.root.generic_string());
    CHECK(first.rendered == 1);

    runner->commands.clear();
    runner->args.clear();
    auto second = service.buildAll(project.root.generic_string());
    CHECK(second.skipped == 1);
    CHECK(second.rendered == 0);
    CHECK(runner->commands.empty());

    // Touching the .mmd (newer mtime) forces a rebuild.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    project.write("diagrams/hero.mmd", "graph TD\n  A --> C\n");
    auto third = service.buildAll(project.root.generic_string());
    CHECK(third.rendered == 1);
    CHECK(third.skipped == 0);
}

TEST_CASE("buildAll surfaces tool failures with the captured log") {
    const auto project = TestProject::make();
    project.write("diagrams/broken.mmd", "graph TD\n  A -->\n");
    project.write("main.tex", "\\includegraphics{diagrams/broken.pdf}\n");

    auto runner = std::make_shared<FakeRunner>();
    installFakeToolchain(project, *runner);
    runner->next_exit_code = 1;
    runner->script_output = "Parse error on line 2";
    core::DiagramBuildService service(runner);

    const auto summary = service.buildAll(project.root.generic_string());
    CHECK(summary.failed == 1);
    CHECK(summary.rendered == 0);
    CHECK(summary.error.find("broken.pdf") != std::string::npos);
    CHECK(summary.error.find("Parse error") != std::string::npos);
}

TEST_CASE("buildAll does nothing without diagrams or a toolchain") {
    const auto project = TestProject::make();
    project.write("main.tex", "\\includegraphics{diagrams/hero.pdf}\n");

    auto runner = std::make_shared<FakeRunner>();
    installFakeToolchain(project, *runner);

    SUBCASE("no .mmd sources: nothing happens") {
        core::DiagramBuildService service(runner);
        const auto summary = service.buildAll(project.root.generic_string());
        CHECK(summary.rendered == 0);
        CHECK(summary.failed == 0);
        CHECK(runner->commands.empty());
    }
    SUBCASE("toolchain missing: zero work, no failure") {
        // Defaults replaced with nothing and no project-local toolchain:
        // nothing can resolve, so the service must degrade to a no-op.
        fs::remove_all(project.root / "node_modules");
        core::DiagramBuildService service(runner);
        service.setSearchPaths({}, true);
        project.write("diagrams/hero.mmd", "graph TD\n  A --> B\n");
        const auto summary = service.buildAll(project.root.generic_string());
        CHECK(summary.rendered == 0);
        CHECK(summary.failed == 0);
        CHECK(runner->commands.empty());
    }

    SUBCASE("toolchain missing with diagram sources reports what is absent") {
        fs::remove_all(project.root / "node_modules");
        core::DiagramBuildService service(runner);
        service.setSearchPaths({}, true);
        project.write("diagrams/hero.mmd", "graph TD\n  A --> B\n");
        const auto summary = service.buildAll(project.root.generic_string());
        CHECK(summary.rendered == 0);
        CHECK(summary.failed == 0);
        REQUIRE(summary.toolchain_error.find("node") != std::string::npos);
        CHECK(summary.toolchain_error.find("mmdc") != std::string::npos);
        CHECK(summary.toolchain_error.find("rsvg-convert") != std::string::npos);
    }
}

// The service must reach the node/rsvg toolchain even when the host PATH
// cannot provide it (GUI apps launch with a minimal PATH).
TEST_CASE("CompileService uses injected search paths for the diagram toolchain") {
    const auto project = TestProject::make();
    project.write("diagrams/flow.mmd", "graph TD\n  A --> B\n");
    project.write("main.tex", "\\includegraphics{diagrams/flow.pdf}\n");

    auto bin_dir = fs::temp_directory_path() / "diagram-locator-bin";
    fs::create_directories(bin_dir);
    for (const char* tool : {"mmdc", "rsvg-convert", "node"}) {
        std::ofstream(bin_dir / tool, std::ios::trunc) << "#!/bin/sh\n";
    }

    auto runner = std::make_shared<FakeRunner>();
    core::DiagramBuildService service(runner);
    service.setSearchPaths({bin_dir.generic_string()}, true);

    const auto summary = service.buildAll(project.root.generic_string());
    CHECK(summary.rendered == 1);
    CHECK(summary.toolchain_error.empty());
    REQUIRE(runner->commands.size() >= 2);
    CHECK(runner->commands[0].find("node") != std::string::npos);
    CHECK(fs::exists(project.root / "diagrams" / "flow.pdf"));

    fs::remove_all(bin_dir);
}

TEST_CASE("toolchain identity stamp invalidates cached artifacts once") {
    const auto project = TestProject::make();
    project.write("diagrams/hero.mmd", "graph TD\n  A --> B\n");
    project.write("main.tex", "\\includegraphics{diagrams/hero.pdf}\n");

    auto bin_dir = fs::temp_directory_path() / "diagram-stamp-bin";
    fs::create_directories(bin_dir);
    for (const char* tool : {"mmdc", "rsvg-convert", "node"}) {
        std::ofstream(bin_dir / tool, std::ios::trunc) << "#!/bin/sh\n";
    }

    auto runner = std::make_shared<FakeRunner>();
    core::DiagramBuildService service(runner);
    service.setSearchPaths({bin_dir.generic_string()}, true);

    // First run: no stamp -> full rebuild, and the stamp is written.
    auto first = service.buildAll(project.root.generic_string());
    CHECK(first.rendered == 1);
    CHECK(first.full_rebuild);
    CHECK(fs::exists(project.root / ".latexcompiler-diagram-stamp"));

    // Second run with the same toolchain: everything is cached.
    runner->commands.clear();
    runner->args.clear();
    auto second = service.buildAll(project.root.generic_string());
    CHECK(second.rendered == 0);
    CHECK(second.skipped == 1);
    CHECK_FALSE(second.full_rebuild);
    CHECK(runner->commands.empty());

    // Changed renderer mtime (an upgrade): one full rebuild, new stamp.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::ofstream(bin_dir / "rsvg-convert", std::ios::app) << "# upgraded\n";
    auto third = service.buildAll(project.root.generic_string());
    CHECK(third.rendered == 1);
    CHECK(third.full_rebuild);

    // And caching resumes on the following run.
    runner->commands.clear();
    auto fourth = service.buildAll(project.root.generic_string());
    CHECK(fourth.skipped == 1);
    CHECK_FALSE(fourth.full_rebuild);

    fs::remove_all(bin_dir);
}

// The engine-side log must say why nothing rendered instead of a bare
// "diagram build: 0 rendered".
TEST_CASE("CompileService reports a missing diagram toolchain in the log") {
    const auto project = TestProject::make();
    project.write("diagrams/flow.mmd", "graph TD\n  A --> B\n");
    project.write("main.tex", "\\includegraphics{diagrams/flow.pdf}\n");

    class LogEvents : public StubEvents {
    public:
        void publish(const core::contracts::Event& event) override {
            ++count;
            if (event.topic == "compile_output") {
                lines.push_back(event.payload);
            }
        }
        std::vector<std::string> lines;
    };

    auto runner = std::make_shared<FakeRunner>();
    auto compiler = std::make_shared<RecordingCompiler>();
    auto events = std::make_shared<LogEvents>();
    core::CompileService service(compiler, std::make_shared<StubStore>(), events);
    auto builder = std::make_shared<core::DiagramBuildService>(runner);
    builder->setSearchPaths({}, true);
    service.setDiagramBuilder(builder);

    core::contracts::CompileRequest request;
    request.projectPath = project.root.generic_string();
    request.mainFile = "main.tex";
    const auto result = service.compileProject(request);

    CHECK(result.success);
    bool diagnosed = false;
    for (const auto& line : events->lines) {
        if (line.find("missing: node") != std::string::npos) {
            diagnosed = true;
        }
    }
    CHECK(diagnosed);
}

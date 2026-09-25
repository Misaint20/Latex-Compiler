#include "adapters/classtex/ClassicTexEngine.hpp"
#include "core/MultiEngineCompiler.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace {

using core::contracts::CompileRequest;
using core::contracts::CompileResult;
using core::contracts::EngineInfo;
using core::contracts::OutputSink;
using core::contracts::ProcessRequest;
using core::contracts::ProcessResult;

class FakeRunner : public core::contracts::IProcessRunner {
public:
    ProcessResult run(const ProcessRequest& request) override {
        return run(request, {});
    }

    ProcessResult run(const ProcessRequest& request, const OutputSink& sink) override {
        ++run_count_;
        commands.push_back(request.command);
        args.push_back(request.args);
        // Simulates the engine writing its PDF next to the source.
        if (!write_pdf_on_run.empty()) {
            std::ofstream output(write_pdf_on_run, std::ios::trunc);
            output << "%PDF-1.4 fake\n";
        }
        if (sink && !script_output.empty()) {
            sink(script_output.data(), script_output.size());
        }
        // The rerun request is only emitted by the first pass, as real logs do.
        if (sink && run_count_ == 1 && !stream_output.empty()) {
            sink(stream_output.data(), stream_output.size());
        }
        return {next_exit_code, script_output, "", false};
    }

    void cancelActive() override { canceled = true; }

    std::vector<std::string> commands;
    std::vector<std::vector<std::string>> args;
    std::string script_output;
    std::string stream_output;
    std::filesystem::path write_pdf_on_run;
    int next_exit_code = 0;
    bool canceled = false;

private:
    int run_count_ = 0;
};

class StubAdapter : public core::contracts::ICompiler {
public:
    StubAdapter(std::vector<EngineInfo> engines, CompileResult result)
        : engines_(std::move(engines)), result_(std::move(result)) {}

    std::vector<EngineInfo> availableEngines() override { return engines_; }
    CompileResult compile(const CompileRequest& request) override {
        last_engine = request.engine;
        return result_;
    }
    CompileResult compile(const CompileRequest& request, core::contracts::ICompileProgress* progress) override {
        ++compile_calls;
        last_engine = request.engine;
        if (progress) {
            progress->onFinished(result_);
        }
        return result_;
    }
    void cancel(core::contracts::CompileId) override { ++cancel_calls; }

    int compile_calls = 0;
    int cancel_calls = 0;
    std::string last_engine;

private:
    std::vector<EngineInfo> engines_;
    CompileResult result_;
};

class MemorySettings : public core::contracts::IAppSettings {
public:
    std::string get(const std::string& key, const std::string& fallback) override {
        const auto it = values.find(key);
        return it == values.end() ? fallback : it->second;
    }
    void set(const std::string& key, const std::string& value) override { values[key] = value; }
    std::map<std::string, std::string> values;
};

std::filesystem::path make_project() {
    static int counter = 0;
    auto dir = std::filesystem::temp_directory_path() /
               ("latex-engine-tests-" + std::to_string(counter++));
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "main.tex") << "\\documentclass{article}\n\\begin{document}hi\n\\end{document}\n";
    return dir;
}

const std::string& firstCommandArgs(const FakeRunner& runner, const std::string& needle) {
    for (const auto& command : runner.commands) {
        if (command.find(needle) != std::string::npos) {
            return command;
        }
    }
    static const std::string empty;
    return empty;
}

} // namespace

TEST_CASE("ClassicTexEngine discovers binaries only in injected dirs when defaults are replaced") {
    auto runner = std::make_shared<FakeRunner>();
    adapters::classtex::ClassicTexEngine engine(runner);

    SUBCASE("no tools anywhere") {
        engine.setSearchPaths({}, true);
        const auto engines = engine.availableEngines();
        REQUIRE(engines.size() == 3);
        for (const auto& e : engines) {
            CHECK_FALSE(e.available);
        }
    }

    SUBCASE("xelatex present in injected dir") {
        auto dir = std::filesystem::temp_directory_path() / "latex-engine-tests-bin";
        std::filesystem::create_directories(dir);
        std::ofstream(dir / "xelatex") << "#!/bin/sh\n";
        std::filesystem::permissions(dir / "xelatex", std::filesystem::perms::owner_exec);
        engine.setSearchPaths({dir.generic_string()}, true);
        const auto engines = engine.availableEngines();
        REQUIRE(engines.size() == 3);
        CHECK(engines[1].id == "xelatex");
        CHECK(engines[1].available);
        CHECK_FALSE(engines[0].available);
        std::filesystem::remove_all(dir);
    }
}

TEST_CASE("ClassicTexEngine runs the full pass chain and reports success with a PDF") {
    const auto project = make_project();
    auto bin_dir = std::filesystem::temp_directory_path() / "latex-engine-tests-bin2";
    std::filesystem::create_directories(bin_dir);
    std::ofstream(bin_dir / "pdflatex") << "#!/bin/sh\n";
    std::filesystem::permissions(bin_dir / "pdflatex", std::filesystem::perms::owner_exec);

    auto runner = std::make_shared<FakeRunner>();
    // Pass 1 asks for one rerun; pass 2 is stable.
    runner->stream_output = "LaTeX Warning: Label(s) may have changed. Rerun to get cross-references right.\n";
    runner->write_pdf_on_run = project / "main.pdf";
    adapters::classtex::ClassicTexEngine engine(runner);
    engine.setSearchPaths({bin_dir.generic_string()}, true);

    const auto result = engine.compile({project.generic_string(), "main.tex", ""});
    CHECK(result.success);
    CHECK(result.outputPath.find("main.pdf") != std::string::npos);
    // Pass 1 + one rerun (log asked for it), then stop.
    REQUIRE(runner->commands.size() == 2);
    for (const auto& argv : runner->args) {
        REQUIRE(argv.size() == 3);
        CHECK(argv[0] == "-interaction=nonstopmode");
        CHECK(argv[1] == "-file-line-error");
    }

    std::filesystem::remove_all(project);
    std::filesystem::remove_all(bin_dir);
}

TEST_CASE("ClassicTexEngine fails with the engine log when the pass dies") {
    const auto project = make_project();
    auto bin_dir = std::filesystem::temp_directory_path() / "latex-engine-tests-bin5";
    std::filesystem::create_directories(bin_dir);
    std::ofstream(bin_dir / "pdflatex") << "#!/bin/sh\n";
    std::filesystem::permissions(bin_dir / "pdflatex", std::filesystem::perms::owner_exec);

    auto runner = std::make_shared<FakeRunner>();
    runner->next_exit_code = 1;
    runner->stream_output = "./main.tex:12: Undefined control sequence.\n";
    adapters::classtex::ClassicTexEngine engine(runner);
    engine.setSearchPaths({bin_dir.generic_string()}, true);

    const auto result = engine.compile({project.generic_string(), "main.tex", ""});
    CHECK_FALSE(result.success);
    CHECK(result.errorMessage.find("Undefined control sequence") != std::string::npos);

    std::filesystem::remove_all(project);
    std::filesystem::remove_all(bin_dir);
}

TEST_CASE("ClassicTexEngine keeps a recovered-errors PDF as success") {
    const auto project = make_project();
    auto bin_dir = std::filesystem::temp_directory_path() / "latex-engine-tests-bin3";
    std::filesystem::create_directories(bin_dir);
    std::ofstream(bin_dir / "xelatex") << "#!/bin/sh\n";
    std::filesystem::permissions(bin_dir / "xelatex", std::filesystem::perms::owner_exec);

    auto runner = std::make_shared<FakeRunner>();
    // Nonstopmode engines exit nonzero after recovered errors but still write.
    runner->next_exit_code = 1;
    runner->write_pdf_on_run = project / "main.pdf";
    adapters::classtex::ClassicTexEngine engine(runner);
    engine.setSearchPaths({bin_dir.generic_string()}, true);

    const auto result = engine.compile({project.generic_string(), "main.tex", ""});
    CHECK(result.success);
    CHECK(result.errorMessage.empty());

    std::filesystem::remove_all(project);
    std::filesystem::remove_all(bin_dir);
}

TEST_CASE("ClassicTexEngine auto-picks a Unicode engine for fontspec documents") {
    const auto project = make_project();
    std::ofstream(project / "main.tex")
        << "\\documentclass{article}\n\\usepackage{fontspec}\n\\begin{document}x\n\\end{document}\n";

    auto xelatex_dir = std::filesystem::temp_directory_path() / "latex-engine-tests-bin4";
    std::filesystem::create_directories(xelatex_dir);
    std::ofstream(xelatex_dir / "xelatex") << "#!/bin/sh\n";
    std::filesystem::permissions(xelatex_dir / "xelatex", std::filesystem::perms::owner_exec);

    auto runner = std::make_shared<FakeRunner>();
    adapters::classtex::ClassicTexEngine engine(runner);
    engine.setSearchPaths({xelatex_dir.generic_string()}, true);
    std::ofstream(project / "main.pdf") << "%PDF-1.4 fake\n";

    (void)engine.compile({project.generic_string(), "main.tex", ""});
    REQUIRE_FALSE(runner->commands.empty());
    CHECK(runner->commands.front().find("xelatex") != std::string::npos);

    std::filesystem::remove_all(project);
    std::filesystem::remove_all(xelatex_dir);
}

TEST_CASE("MultiEngineCompiler prefers the stored engine and falls back when missing") {
    auto settings = std::make_shared<MemorySettings>();
    CompileResult ok{true, "out.pdf", {}, {}};

    auto classic = std::make_shared<StubAdapter>(
        std::vector<EngineInfo>{{"pdflatex", "pdfLaTeX", false}, {"xelatex", "XeLaTeX", true}}, ok);
    auto tectonic = std::make_shared<StubAdapter>(
        std::vector<EngineInfo>{{"tectonic", "Tectonic", false}}, ok);

    core::MultiEngineCompiler selector(
        {{"classic", classic}, {"tectonic", tectonic}}, settings);

    SUBCASE("auto runs the first available engine without notice") {
        const auto result = selector.compile({"", "main.tex", ""});
        CHECK(result.success);
        CHECK(result.notice.empty());
        CHECK(classic->compile_calls == 1);
        CHECK(tectonic->compile_calls == 0);
        // Auto mode leaves the adapter id empty so the adapter can pick per
        // document (fontspec documents must not get pdfLaTeX).
        CHECK(classic->last_engine.empty());
    }

    SUBCASE("manual engine pick reaches the adapter without the backend suffix") {
        settings->set("compile.engine", "xelatex@classic");
        const auto result = selector.compile({"", "main.tex", ""});
        CHECK(result.success);
        CHECK(classic->last_engine == "xelatex");
        CHECK(tectonic->compile_calls == 0);
    }

    SUBCASE("missing preferred engine falls through and explains the fallback") {
        settings->set("compile.engine", "pdflatex@classic");
        const auto result = selector.compile({"", "main.tex", ""});
        CHECK(result.success);
        CHECK(result.notice.find("xelatex@classic") != std::string::npos);
        CHECK(classic->compile_calls == 1);
    }

    SUBCASE("unknown engine id is rejected for persistence") {
        CHECK(selector.isKnownEngine("xelatex@classic"));
        CHECK_FALSE(selector.isKnownEngine("nonsense"));
        CHECK(selector.isKnownEngine("auto"));
    }

    SUBCASE("metadata is exposed with backend suffix") {
        const auto engines = selector.availableEngines();
        REQUIRE(engines.size() == 3);
        CHECK(engines[0].id == "pdflatex@classic");
        CHECK(engines[1].id == "xelatex@classic");
        CHECK(engines[2].id == "tectonic@tectonic");
    }

    SUBCASE("cancel reaches the active adapter") {
        (void)selector.compile({"", "main.tex", ""});
        selector.cancel("x");
        CHECK(classic->cancel_calls == 1);
        CHECK(tectonic->cancel_calls == 0);
    }
}

// Real Store-Test/docs layout: the main file loads its preamble package via
// a path macro, and fontspec lives inside that package — the engine must
// follow project-local includes before picking pdfLaTeX.
TEST_CASE("ClassicTexEngine auto-picks a Unicode engine when fontspec lives in a local package") {
    auto project = std::filesystem::temp_directory_path() / "latex-engine-tests-sty";
    std::filesystem::create_directories(project / "latex");
    std::ofstream(project / "main.tex")
        << "\\documentclass{book}\n"
        << "\\def\\pkgpath{latex}\n"
        << "\\usepackage{\\pkgpath/axon}\n"
        << "\\begin{document}x\\end{document}\n";
    std::ofstream(project / "latex" / "axon.sty") << "\\usepackage{fontspec}\n";

    auto xelatex_dir = std::filesystem::temp_directory_path() / "latex-engine-tests-bin6";
    std::filesystem::create_directories(xelatex_dir);
    std::ofstream(xelatex_dir / "xelatex") << "#!/bin/sh\n";
    std::filesystem::permissions(xelatex_dir / "xelatex", std::filesystem::perms::owner_exec);

    auto runner = std::make_shared<FakeRunner>();
    adapters::classtex::ClassicTexEngine engine(runner);
    engine.setSearchPaths({xelatex_dir.generic_string()}, true);

    (void)engine.compile({project.generic_string(), "main.tex", ""});
    REQUIRE_FALSE(runner->commands.empty());
    CHECK(runner->commands.front().find("xelatex") != std::string::npos);

    std::filesystem::remove_all(project);
    std::filesystem::remove_all(xelatex_dir);
}

TEST_CASE("ClassicTexEngine follows \\input to a local wrapper that loads fontspec") {
    auto project = std::filesystem::temp_directory_path() / "latex-engine-tests-input";
    std::filesystem::create_directories(project);
    std::ofstream(project / "main.tex")
        << "\\documentclass{article}\n\\input{preamble/setup}\n\\begin{document}x\\end{document}\n";
    std::filesystem::create_directories(project / "preamble");
    std::ofstream(project / "preamble" / "setup.tex") << "\\usepackage{unicode-math}\n";

    auto xelatex_dir = std::filesystem::temp_directory_path() / "latex-engine-tests-bin7";
    std::filesystem::create_directories(xelatex_dir);
    std::ofstream(xelatex_dir / "xelatex") << "#!/bin/sh\n";
    std::filesystem::permissions(xelatex_dir / "xelatex", std::filesystem::perms::owner_exec);

    auto runner = std::make_shared<FakeRunner>();
    adapters::classtex::ClassicTexEngine engine(runner);
    engine.setSearchPaths({xelatex_dir.generic_string()}, true);

    (void)engine.compile({project.generic_string(), "main.tex", ""});
    REQUIRE_FALSE(runner->commands.empty());
    CHECK(runner->commands.front().find("xelatex") != std::string::npos);

    std::filesystem::remove_all(project);
    std::filesystem::remove_all(xelatex_dir);
}

TEST_CASE("ClassicTexEngine never picks pdfLaTeX for fontspec docs, even when installed") {
    auto project = std::filesystem::temp_directory_path() / "latex-engine-tests-both";
    std::filesystem::create_directories(project);
    std::ofstream(project / "main.tex")
        << "\\documentclass{article}\n\\usepackage{fontspec}\n\\begin{document}x\\end{document}\n";

    auto bin_dir = std::filesystem::temp_directory_path() / "latex-engine-tests-bin8";
    std::filesystem::create_directories(bin_dir);
    for (const char* tool : {"pdflatex", "xelatex"}) {
        std::ofstream(std::string(bin_dir.generic_string()) + "/" + tool) << "#!/bin/sh\n";
        std::filesystem::permissions(bin_dir / tool, std::filesystem::perms::owner_exec);
    }

    auto runner = std::make_shared<FakeRunner>();
    adapters::classtex::ClassicTexEngine engine(runner);
    engine.setSearchPaths({bin_dir.generic_string()}, true);

    (void)engine.compile({project.generic_string(), "main.tex", ""});
    REQUIRE_FALSE(runner->commands.empty());
    CHECK(runner->commands.front().find("pdflatex") == std::string::npos);
    CHECK(runner->commands.front().find("xelatex") != std::string::npos);

    std::filesystem::remove_all(project);
    std::filesystem::remove_all(bin_dir);
}

TEST_CASE("ClassicTexEngine plain documents still prefer pdfLaTeX when available") {
    auto project = std::filesystem::temp_directory_path() / "latex-engine-tests-plain";
    std::filesystem::create_directories(project);
    std::ofstream(project / "main.tex")
        << "\\documentclass{article}\n\\usepackage{graphicx}\n\\begin{document}x\\end{document}\n";

    auto bin_dir = std::filesystem::temp_directory_path() / "latex-engine-tests-bin9";
    std::filesystem::create_directories(bin_dir);
    for (const char* tool : {"pdflatex", "xelatex"}) {
        std::ofstream(std::string(bin_dir.generic_string()) + "/" + tool) << "#!/bin/sh\n";
        std::filesystem::permissions(bin_dir / tool, std::filesystem::perms::owner_exec);
    }

    auto runner = std::make_shared<FakeRunner>();
    adapters::classtex::ClassicTexEngine engine(runner);
    engine.setSearchPaths({bin_dir.generic_string()}, true);

    (void)engine.compile({project.generic_string(), "main.tex", ""});
    REQUIRE_FALSE(runner->commands.empty());
    CHECK(runner->commands.front().find("pdflatex") != std::string::npos);

    std::filesystem::remove_all(project);
    std::filesystem::remove_all(bin_dir);
}

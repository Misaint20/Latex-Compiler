#include "adapters/process/SystemProcessRunner.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#ifdef _WIN32
static const char* kTrue = "cmd";
#else
static const char* kTrue = "true";
#endif

TEST_CASE("SystemProcessRunner reports exit codes faithfully") {
    adapters::process::SystemProcessRunner runner;

    SUBCASE("success") {
        core::contracts::ProcessRequest request;
        request.command = kTrue;
        const auto result = runner.run(request);
        CHECK(result.exit_code == 0);
    }

    SUBCASE("missing binary") {
        core::contracts::ProcessRequest request;
        request.command = "definitely-not-a-real-binary-1234";
        const auto result = runner.run(request);
        CHECK(result.exit_code != 0);
    }
}

TEST_CASE("SystemProcessRunner captures output and respects working directory") {
    adapters::process::SystemProcessRunner runner;

    const auto temp_dir = std::filesystem::temp_directory_path() /
                          "latex-runner-test" / std::to_string(std::rand());
    std::error_code ec;
    std::filesystem::create_directories(temp_dir, ec);
    REQUIRE_FALSE(ec.value() != 0);

    {
        std::ofstream marker(temp_dir / "marker.txt");
        marker << "content-marker" << std::endl;
    }

    core::contracts::ProcessRequest request;
    request.command = "ls";
    request.working_directory = temp_dir.generic_string();
    const auto result = runner.run(request);

    CHECK(result.exit_code == 0);
    CHECK(result.std_out.find("marker.txt") != std::string::npos);

    std::filesystem::remove_all(temp_dir, ec);
}

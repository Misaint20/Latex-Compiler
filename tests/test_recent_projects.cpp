#include "adapters/history/JsonRecentProjects.hpp"
#include "core/ProjectService.hpp"
#include "core/contracts/IProjectScanner.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

using adapters::history::JsonRecentProjects;

// The adapter canonicalizes paths so /tmp/x and /private/tmp/x (macOS)
// deduplicate to one entry; tests compare against the canonical form.
std::string canon(const std::string& path) {
    return std::filesystem::weakly_canonical(path).generic_string();
}

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

class StubScanner : public core::contracts::IProjectScanner {
public:
    core::contracts::ProjectInfo scan(const std::string& path) override {
        ++scan_calls;
        if (empty_result) {
            return {path, {}, "", {}, {}, {}};
        }
        return {path, {"main.tex"}, "main.tex", {}, {}, {}};
    }

    int scan_calls = 0;
    bool empty_result = false;
};

} // namespace

TEST_CASE("JsonRecentProjects records MRU ordered and deduplicated") {
    auto files = std::make_shared<FakeFileStore>();
    JsonRecentProjects recents(files, "/tmp/lc-recents-test");

    recents.record("/tmp/proj-a");
    recents.record("/tmp/proj-b");
    recents.record("/tmp/proj-a");

    const auto list = recents.list();
    REQUIRE(list.size() == 2);
    CHECK(list[0].path == canon("/tmp/proj-a"));
    CHECK(list[1].path == canon("/tmp/proj-b"));
    CHECK(list[0].last_opened_ms > 0);
}

TEST_CASE("JsonRecentProjects persists across instances and caps entries") {
    auto files = std::make_shared<FakeFileStore>();
    {
        JsonRecentProjects recents(files, "/tmp/lc-recents-test");
        for (int i = 0; i < 14; ++i) {
            recents.record("/tmp/proj-" + std::to_string(i));
        }
    }

    JsonRecentProjects reloaded(files, "/tmp/lc-recents-test");
    const auto list = reloaded.list();
    REQUIRE(list.size() == JsonRecentProjects::kMaxEntries);
    CHECK(list.front().path == canon("/tmp/proj-13"));
    CHECK(list.back().path == canon("/tmp/proj-4"));
}

TEST_CASE("list marks missing folders and removeMissing purges only them") {
    const auto live_dir = "/tmp/lc-recents-live";
    const auto gone_dir = "/tmp/lc-recents-gone";
    std::filesystem::remove_all(live_dir);
    std::filesystem::create_directories(live_dir);
    std::filesystem::remove_all(gone_dir);

    auto files = std::make_shared<FakeFileStore>();
    JsonRecentProjects recents(files, "/tmp/lc-recents-test");
    // Record gone last so it sits at the MRU head, like a deleted recent project.
    recents.record(gone_dir);
    recents.record(live_dir);

    auto list = recents.list();
    REQUIRE(list.size() == 2);
    CHECK(list[0].path == canon(live_dir));
    CHECK(list[0].exists == true);
    CHECK(list[1].path == canon(gone_dir));
    CHECK(list[1].exists == false);

    // Opening the live project flips the MRU order but staleness follows it.
    recents.record(live_dir);
    list = recents.list();
    CHECK(list[0].exists == true);
    CHECK(list[1].exists == false);

    // The purge drops exactly the missing entries.
    CHECK(recents.removeMissing() == 1);
    list = recents.list();
    REQUIRE(list.size() == 1);
    CHECK(list[0].path == canon(live_dir));
    CHECK(list[0].exists == true);

    // Purging again is a no-op.
    CHECK(recents.removeMissing() == 0);

    std::filesystem::remove_all(live_dir);
    std::filesystem::remove_all(gone_dir);
}

TEST_CASE("rememberMainFile persists the choice across instances") {
    auto files = std::make_shared<FakeFileStore>();
    {
        JsonRecentProjects recents(files, "/tmp/lc-recents-test");
        recents.record("/tmp/proj-a");
        recents.rememberMainFile("/tmp/proj-a", "capitulos/principal.tex");
    }

    JsonRecentProjects reloaded(files, "/tmp/lc-recents-test");
    const auto list = reloaded.list();
    REQUIRE(list.size() == 1);
    CHECK(list[0].main_file == "capitulos/principal.tex");

    // Re-opening the project keeps the remembered choice.
    reloaded.record("/tmp/proj-a");
    const auto after_reopen = reloaded.list();
    REQUIRE(after_reopen.size() == 1);
    CHECK(after_reopen[0].main_file == "capitulos/principal.tex");

    // Overwriting works too.
    reloaded.rememberMainFile("/tmp/proj-a", "otro.tex");
    CHECK(reloaded.list()[0].main_file == "otro.tex");
}

TEST_CASE("JsonRecentProjects removes entries and reports misses") {
    auto files = std::make_shared<FakeFileStore>();
    JsonRecentProjects recents(files, "/tmp/lc-recents-test");
    recents.record("/tmp/proj-a");
    recents.record("/tmp/proj-b");

    CHECK(recents.remove("/tmp/proj-a"));
    CHECK_FALSE(recents.remove("/tmp/proj-a"));
    CHECK_FALSE(recents.remove("/tmp/never-opened"));

    const auto list = recents.list();
    REQUIRE(list.size() == 1);
    CHECK(list[0].path == canon("/tmp/proj-b"));
}

TEST_CASE("JsonRecentProjects tolerates a corrupt file and empty records") {
    auto files = std::make_shared<FakeFileStore>();
    JsonRecentProjects writer(files, "/tmp/lc-recents-test");
    writer.record("/tmp/proj-a");

    for (auto& [path, content] : files->files) {
        content = "][ not json";
    }

    JsonRecentProjects reader(files, "/tmp/lc-recents-test");
    CHECK(reader.list().empty());
    reader.record("/tmp/proj-b");

    const auto list = reader.list();
    REQUIRE(list.size() == 1);
    CHECK(list[0].path == canon("/tmp/proj-b"));

    // Empty paths are ignored, not stored.
    reader.record("");
    CHECK(reader.list().size() == 1);
}

TEST_CASE("scanProject records recents only for projects with TeX files") {
    auto scanner = std::make_shared<StubScanner>();
    auto recents_store = std::make_shared<FakeFileStore>();
    auto recents = std::make_shared<JsonRecentProjects>(recents_store, "/tmp/lc-recents-test");
    core::ProjectService service(scanner, nullptr, nullptr, nullptr, recents);

    service.scanProject("/tmp/good-project");
    const auto list = recents->list();
    REQUIRE(list.size() == 1);
    CHECK(list[0].path == canon("/tmp/good-project"));

    scanner->empty_result = true;
    service.scanProject("/tmp/empty-folder");
    CHECK(recents->list().size() == 1);
}

TEST_CASE("existence TTL cache serves instantly and re-probes after expiry") {
    const auto live_dir = "/tmp/lc-recents-ttl-live";
    std::filesystem::remove_all(live_dir);
    std::filesystem::create_directories(live_dir);
    const auto gone_dir = "/tmp/lc-recents-ttl-gone";
    std::filesystem::remove_all(gone_dir);

    auto files = std::make_shared<FakeFileStore>();
    std::int64_t now = 1'000'000;
    JsonRecentProjects recents(files, "/tmp/lc-recents-test", std::chrono::seconds(60),
                               [&] { return now; });
    recents.record(gone_dir);
    recents.record(live_dir);

    // First list pays one probe round and caches the verdicts.
    auto list = recents.list();
    REQUIRE(list.size() == 2);
    CHECK(list[0].exists == true);
    CHECK(list[1].exists == false);

    // The folder disappears; within the TTL the cached verdict still stands.
    std::filesystem::remove_all(live_dir);
    list = recents.list();
    REQUIRE(list.size() == 2);
    CHECK(list[0].exists == true);

    // After the TTL lapses the next list re-probes and reports reality.
    now += 60'001;
    list = recents.list();
    REQUIRE(list.size() == 2);
    CHECK(list[0].exists == false);
    CHECK(list[1].exists == false);

    std::filesystem::remove_all(live_dir);
}

TEST_CASE("a clock moved backwards distrusts the cached existence verdicts") {
    const auto live_dir = "/tmp/lc-recents-skew-live";
    std::filesystem::remove_all(live_dir);
    std::filesystem::create_directories(live_dir);

    auto files = std::make_shared<FakeFileStore>();
    std::int64_t now = 5'000'000;
    JsonRecentProjects recents(files, "/tmp/lc-recents-test", std::chrono::seconds(60),
                               [&] { return now; });
    recents.record(live_dir);
    CHECK(recents.list()[0].exists == true);

    std::filesystem::remove_all(live_dir);

    // NTP-style correction moves the clock back: cached verdicts are dropped.
    now -= 10'000;
    CHECK(recents.list()[0].exists == false);

    std::filesystem::remove_all(live_dir);
}

TEST_CASE("removeMissingForceProbe bypasses the TTL cache") {
    const auto live_dir = "/tmp/lc-recents-force-live";
    std::filesystem::remove_all(live_dir);
    std::filesystem::create_directories(live_dir);

    auto files = std::make_shared<FakeFileStore>();
    std::int64_t now = 2'000'000;
    JsonRecentProjects recents(files, "/tmp/lc-recents-test", std::chrono::seconds(60),
                               [&] { return now; });
    recents.record(live_dir);
    CHECK(recents.list()[0].exists == true);

    // The folder vanishes but the cache is still fresh: plain purge sees a
    // stale verdict and removes nothing...
    std::filesystem::remove_all(live_dir);
    CHECK(recents.removeMissing() == 0);

    // ...while the forced re-probe catches the deletion immediately.
    CHECK(recents.removeMissingForceProbe() == 1);
    CHECK(recents.list().empty());

    std::filesystem::remove_all(live_dir);
}

TEST_CASE("ProjectService works without a recents store (backward compatible)") {
    auto scanner = std::make_shared<StubScanner>();
    core::ProjectService service(scanner, nullptr, nullptr, nullptr, nullptr);

    service.scanProject("/tmp/project");
    service.recordRecentProject("/tmp/project");
    CHECK(service.recentProjects().empty());
    CHECK_FALSE(service.removeRecentProject("/tmp/project"));
}

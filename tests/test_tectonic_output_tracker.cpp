#include "adapters/tectonic/TectonicOutputTracker.hpp"

#include <doctest/doctest.h>

#include <string>

namespace {

using adapters::tectonic::TectonicOutputTracker;

void feed(TectonicOutputTracker& tracker, const std::string& log) {
    std::string line;
    for (const char c : log) {
        if (c == '\n') {
            tracker.feed(line);
            line.clear();
        } else {
            line.push_back(c);
        }
    }
    if (!line.empty()) {
        tracker.feed(line);
    }
}

} // namespace

TEST_CASE("Note lines map to canonical stage keys in order") {
    TectonicOutputTracker tracker;
    feed(tracker,
         "note: Running TeX ...\n"
         "note: Rerunning TeX because \"main.aux\" changed ...\n"
         "note: Running xdvipdfmx ...\n"
         "note: Writing `main.pdf` (8 KiB)\n");

    CHECK(tracker.stage_key() == "write");
    CHECK(tracker.percent() == 95);
}

TEST_CASE("Download lines carry the download stage key") {
    TectonicOutputTracker tracker;
    tracker.feed("note: downloading cmr10.tfm");

    CHECK(tracker.stage_key() == "download");
    CHECK(tracker.percent() > 0);
}

TEST_CASE("Warm single-pass run reaches 95 percent before the final PDF check") {
    TectonicOutputTracker tracker;
    feed(tracker,
         "note: \"version 2\" Tectonic command-line interface activated\n"
         "note: Running TeX ...\n"
         "note: Rerunning TeX because \"main.aux\" changed ...\n"
         "note: Running xdvipdfmx ...\n"
         "note: Writing `main.pdf` (8.0234375 KiB)\n"
         "note: Skipped writing 1 intermediate files (use --keep-intermediates to keep them)\n");

    CHECK(tracker.percent() == 95);
    CHECK(tracker.tex_passes() == 1);
    CHECK(tracker.saw_output());
    CHECK(tracker.stage() == "Escribiendo PDF");
}

TEST_CASE("Cold run starts in the download range and climbs monotonically") {
    TectonicOutputTracker tracker;
    std::string log = "note: \"version 2\" Tectonic command-line interface activated\n";
    for (int i = 0; i < 30; ++i) {
        log += "note: downloading font" + std::to_string(i) + ".tfm\n";
    }
    log += "note: Running TeX ...\n"
           "note: Running xdvipdfmx ...\n"
           "note: Writing `main.pdf` (8 KiB)\n";
    feed(tracker, log);

    CHECK(tracker.percent() == 95);
    CHECK(tracker.stage() == "Escribiendo PDF");
    // Downloads before any TeX pass stay below 12 percent.
    TectonicOutputTracker early;
    feed(early, "note: downloading a.tfm\nnote: downloading b.tfm\nnote: downloading c.tfm\n");
    CHECK(early.percent() <= 11);
    CHECK(early.stage() == "Descargando paquetes");
}

TEST_CASE("Multi-pass rerun reports the additional pass") {
    TectonicOutputTracker tracker;
    feed(tracker,
         "note: Running TeX ...\n"
         "note: Rerunning TeX because \"main.aux\" changed ...\n"
         "note: Running TeX ...\n"
         "note: Running xdvipdfmx ...\n"
         "note: Writing `main.pdf` (8 KiB)\n");

    CHECK(tracker.tex_passes() == 2);
    CHECK(tracker.stage() == "Escribiendo PDF");
    CHECK(tracker.percent() == 95);
}

TEST_CASE("Error lines reveal the current input file") {
    TectonicOutputTracker tracker;
    tracker.feed("note: Running TeX ...");
    tracker.feed("error: capitulos/intro.tex:12: Undefined control sequence");

    CHECK(tracker.file() == "capitulos/intro.tex");
    CHECK(tracker.percent() == 15);
    CHECK(tracker.in_tex_pass());
}

TEST_CASE("Unrelated lines never move the progress backwards") {
    TectonicOutputTracker tracker;
    tracker.feed("note: Running TeX ...");
    CHECK(tracker.percent() == 15);

    tracker.feed("note: Skipped writing 1 intermediate files");
    tracker.feed("random noise without prefix");
    tracker.feed("error: halted on potentially-recoverable error as specified");
    CHECK(tracker.percent() == 15);
    CHECK(tracker.file().empty());
}

TEST_CASE("Error lines without a file:line shape are ignored") {
    TectonicOutputTracker tracker;
    tracker.feed("note: Running TeX ...");
    tracker.feed("error: halted on potentially-recoverable error as specified");
    tracker.feed("error: La plataforma se cayó");

    CHECK(tracker.file().empty());
}

TEST_CASE("Progress never decreases across stages") {
    TectonicOutputTracker tracker;
    tracker.feed("note: Running TeX ...");
    const int after_tex = tracker.percent();
    tracker.feed("note: Running xdvipdfmx ...");
    const int after_assembly = tracker.percent();
    tracker.feed("note: Writing `main.pdf` (5 KiB)");
    const int after_writing = tracker.percent();

    CHECK(after_tex < after_assembly);
    CHECK(after_assembly < after_writing);
}

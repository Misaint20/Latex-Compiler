#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace adapters {
namespace tectonic {

struct ProgressSample {
    int percent = 0;       // Non-zero when the line moved the progress forward.
    std::string stage;     // Human-readable label for the current phase.
    std::string stage_key; // Canonical key: download | tex | rerun | assemble | write.
    std::string file;      // Current TeX input file, when known.
};

// A tiny line-based state machine over real Tectonic output. Phases were
// captured from actual runs:
//   "note: Running TeX ..."            -> "tex"
//   "note: Rerunning TeX because ..."  -> "rerun"
//   "note: Running xdvipdfmx ..."      -> "assemble"
//   "note: Writing `main.pdf` (...)"   -> "write"
//   "note: downloading <file>"         -> "download"
// Error lines look like "error: <path>:<line>:" and reveal the input file.
class TectonicOutputTracker {
public:
    ProgressSample feed(const std::string& line) {
        if (line.rfind("note: ", 0) == 0) {
            return handle_note(line.substr(6));
        }
        if (line.rfind("error: ", 0) == 0) {
            return handle_error(line.substr(7));
        }
        return {};
    }

    int percent() const { return percent_; }
    const std::string& stage() const { return stage_; }
    const std::string& stage_key() const { return stage_key_; }
    const std::string& file() const { return file_; }
    bool saw_output() const { return saw_writing_; }
    bool in_tex_pass() const { return tex_passes_ > 0; }
    int tex_passes() const { return tex_passes_; }

private:
    static std::string trim_copy(const std::string& value) {
        std::size_t begin = 0;
        std::size_t end = value.size();
        while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
            ++begin;
        }
        while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
            --end;
        }
        return value.substr(begin, end - begin);
    }

    static bool has_prefix(const std::string& text, const char* prefix) {
        return text.rfind(prefix, 0) == 0;
    }

    // Extracts `<name>:<line>:` from error text, or an empty string.
    static std::string error_file(const std::string& text) {
        const std::size_t colon = text.find(':');
        if (colon == std::string::npos || colon == 0) {
            return {};
        }
        const std::size_t second = text.find(':', colon + 1);
        if (second == std::string::npos) {
            return {};
        }
        const std::string name = trim_copy(text.substr(0, colon));
        for (const char c : text.substr(colon + 1, second - colon - 1)) {
            if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
                return {};
            }
        }
        return name;
    }

    void set_stage(std::string stage, std::string key) {
        stage_ = std::move(stage);
        stage_key_ = std::move(key);
    }

    void set_percent(const int percent) {
        percent_ = std::max(percent_, std::min(percent, 100));
    }

    ProgressSample handle_note(const std::string& body) {
        ProgressSample sample;
        if (has_prefix(body, "Running TeX")) {
            ++tex_passes_;
            if (tex_passes_ > 1) {
                set_stage("Compilando (pasada adicional)", "rerun");
                set_percent(60);
            } else {
                set_stage("Compilando TeX", "tex");
                set_percent(15);
            }
            sample.stage = stage_;
            sample.stage_key = stage_key_;
            sample.percent = percent_;
            return sample;
        }
        if (has_prefix(body, "Rerunning TeX")) {
            set_stage("Compilando (pasada adicional)", "rerun");
            set_percent(60);
            sample.stage = stage_;
            sample.stage_key = stage_key_;
            sample.percent = percent_;
            return sample;
        }
        if (has_prefix(body, "Running xdvipdfmx")) {
            set_stage("Ensamblado del PDF", "assemble");
            set_percent(85);
            sample.stage = stage_;
            sample.stage_key = stage_key_;
            sample.percent = percent_;
            return sample;
        }
        if (has_prefix(body, "Writing `")) {
            saw_writing_ = true;
            set_stage("Escribiendo PDF", "write");
            set_percent(95);
            sample.stage = stage_;
            sample.stage_key = stage_key_;
            sample.percent = percent_;
            return sample;
        }
        if (has_prefix(body, "downloading ")) {
            set_stage("Descargando paquetes", "download");
            const int floor = tex_passes_ > 0 ? 8 : 2;
            const int ceiling = tex_passes_ > 0 ? 14 : 11;
            if (download_count_ < 60) {
                ++download_count_;
            }
            const int scaled = floor + (ceiling - floor) * download_count_ / 60;
            set_percent(scaled);
            sample.stage = stage_;
            sample.stage_key = stage_key_;
            sample.percent = percent_;
            return sample;
        }
        return sample;
    }

    ProgressSample handle_error(const std::string& text) {
        ProgressSample sample;
        const std::string name = error_file(text);
        if (!name.empty()) {
            file_ = name;
            sample.file = file_;
        }
        return sample;
    }

    int percent_ = 0;
    int download_count_ = 0;
    int tex_passes_ = 0;
    bool saw_writing_ = false;
    std::string stage_;
    std::string stage_key_;
    std::string file_;
};

} // namespace tectonic
} // namespace adapters

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace core {
namespace contracts {

struct HistoryEntry {
    std::int64_t timestamp_ms = 0; // Unix epoch milliseconds when the job started.
    std::int64_t duration_ms = 0;
    std::string result; // "succeeded" | "failed" | "canceled"
    std::string main_file;
    std::string output_path;
    std::string error_message;
    // Canonical stage keys reached during the job ("tex", "assemble", ...) and
    // the wall time each stage took, when measured.
    std::vector<std::string> stages_reached;
    std::map<std::string, std::int64_t> stage_durations_ms;
    // Non-fatal engine notice (e.g. auto-selection fallback), when any.
    std::string notice;
};

class ICompileHistory {
public:
    virtual ~ICompileHistory() = default;

    virtual std::vector<HistoryEntry> entries(const std::string& projectPath) = 0;
    virtual void record(const std::string& projectPath, const HistoryEntry& entry) = 0;
};

} // namespace contracts
} // namespace core

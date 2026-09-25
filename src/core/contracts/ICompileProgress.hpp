#pragma once

#include <string>

namespace core {
namespace contracts {

struct CompileResult;

// Snapshot of compiler progress. stage_key is a canonical identifier
// ("download" | "tex" | "rerun" | "assemble" | "write") so the UI can build a
// named pipeline; stage is the human-readable label for the same moment.
struct ProgressInfo {
    int percent = 0;
    std::string stage;
    std::string stage_key;
    std::string file;
};

class ICompileProgress {
public:
    virtual ~ICompileProgress() = default;
    // Called from the compiler's worker context with raw output chunks.
    virtual void onOutput(const std::string& chunk) = 0;
    // Called when the compiler derived new progress from its output.
    virtual void onProgress(const ProgressInfo& info) = 0;
    // Optional eager completion notification; the returned CompileResult
    // remains the source of truth for callers.
    virtual void onFinished(const CompileResult& result) = 0;
};

} // namespace contracts
} // namespace core

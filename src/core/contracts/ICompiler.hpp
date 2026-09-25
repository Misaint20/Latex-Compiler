#pragma once

#include "ICompileProgress.hpp"

#include <string>
#include <vector>

namespace core {
namespace contracts {

struct CompileRequest {
    std::string projectPath;
    std::string mainFile;
    // Engine id chosen by the caller ("" = engine's own auto selection).
    std::string engine;
};

struct CompileResult {
    bool success;
    std::string outputPath;
    std::string errorMessage;
    // When auto-selection fell back to another engine, a message describing
    // why; the UI surfaces it after the job ends.
    std::string notice;
};

using CompileId = std::string;

// A compile engine behind the common ICompiler contract. id is a stable
// ASCII token ("tectonic", "pdflatex", ...); title is a human label.
struct EngineInfo {
    std::string id;
    std::string title;
    bool available = false;
};

class ICompiler {
public:
    virtual ~ICompiler() = default;

    // Metadata for settings UI and auto-selection; cheap and side-effect free.
    virtual std::vector<EngineInfo> availableEngines() = 0;

    virtual CompileResult compile(const CompileRequest& request) = 0;

    // Streaming variant. Implementations should push incremental output into
    // progress and call onFinished when done; the returned result stays the
    // source of truth. The default bridges to the plain overload.
    virtual CompileResult compile(const CompileRequest& request, ICompileProgress* progress) {
        auto result = compile(request);
        if (progress != nullptr) {
            progress->onFinished(result);
        }
        return result;
    }

    virtual void cancel(CompileId id) = 0;
};

} // namespace contracts
} // namespace core

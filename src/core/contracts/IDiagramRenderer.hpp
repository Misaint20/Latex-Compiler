#pragma once

#include <string>

namespace core {
namespace contracts {

struct DiagramSource {
    std::string code;
    std::string type;
};

struct RenderOptions {
    std::string format;
    float scale = 1.0f;
};

struct RenderedDiagram {
    bool success;
    std::string data;
    std::string errorMessage;
};

class IDiagramRenderer {
public:
    virtual ~IDiagramRenderer() = default;
    virtual RenderedDiagram render(const DiagramSource& source, const RenderOptions& options) = 0;
};

} // namespace contracts
} // namespace core

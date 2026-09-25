#pragma once

#include "core/contracts/IDiagramRenderer.hpp"

#include <string>

namespace adapters {
namespace mock {

class FakeDiagramRenderer : public core::contracts::IDiagramRenderer {
public:
    core::contracts::RenderedDiagram render(const core::contracts::DiagramSource& source,
                                            const core::contracts::RenderOptions&) override {
        if (source.code.empty()) {
            return {false, {}, "Diagram source is empty."};
        }
        std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\"><text>";
        svg += source.code;
        svg += "</text></svg>";
        return {true, std::move(svg), {}};
    }
};

} // namespace mock
} // namespace adapters

#pragma once

#include "contracts/IDiagramRenderer.hpp"
#include "contracts/IDiagramCache.hpp"
#include <memory>
#include <string>

namespace core {

class DiagramService {
public:
    DiagramService(
        std::shared_ptr<contracts::IDiagramRenderer> renderer,
        std::shared_ptr<contracts::IDiagramCache> cache
    );

    contracts::RenderedDiagram renderDiagram(const contracts::DiagramSource& source, const contracts::RenderOptions& options);

private:
    std::shared_ptr<contracts::IDiagramRenderer> renderer_;
    std::shared_ptr<contracts::IDiagramCache> cache_;
    std::string computeHash(const contracts::DiagramSource& source, const contracts::RenderOptions& options) const;
};

} // namespace core

#include "DiagramService.hpp"
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace core
{

    DiagramService::DiagramService(
        std::shared_ptr<contracts::IDiagramRenderer> renderer,
        std::shared_ptr<contracts::IDiagramCache> cache)
        : renderer_(std::move(renderer)),
          cache_(std::move(cache)) {}

    std::string DiagramService::computeHash(const contracts::DiagramSource &source, const contracts::RenderOptions &options) const
    {
        size_t h1 = std::hash<std::string>{}(source.code);
        size_t h2 = std::hash<std::string>{}(options.format);
        return std::to_string(h1 ^ (h2 << 1));
    }

    contracts::RenderedDiagram DiagramService::renderDiagram(const contracts::DiagramSource &source, const contracts::RenderOptions &options)
    {
        std::string hashKey = computeHash(source, options);
        auto cached = cache_->get(hashKey);
        if (cached)
        {
            return {true, *cached, ""};
        }

        auto result = renderer_->render(source, options);
        if (result.success)
        {
            cache_->set(hashKey, result.data);
        }
        return result;
    }

} // namespace core

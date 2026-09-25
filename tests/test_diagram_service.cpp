#include "core/DiagramService.hpp"

#include <doctest/doctest.h>
#include <memory>

namespace {

class FakeRenderer : public core::contracts::IDiagramRenderer {
public:
    core::contracts::RenderedDiagram render(const core::contracts::DiagramSource& source,
                                            const core::contracts::RenderOptions&) override {
        ++render_calls;
        last_code = source.code;
        return {true, "<svg>" + source.code + "</svg>", {}};
    }

    std::string last_code;
    int render_calls = 0;
};

class FakeCache : public core::contracts::IDiagramCache {
public:
    std::optional<std::string> get(const std::string& key) override {
        const auto found = entries.find(key);
        return found == entries.end() ? std::nullopt : std::optional{found->second};
    }
    void set(const std::string& key, const std::string& value) override {
        entries[key] = value;
    }
    std::unordered_map<std::string, std::string> entries;
};

} // namespace

TEST_CASE("DiagramService renders once and caches the result") {
    auto renderer = std::make_shared<FakeRenderer>();
    auto cache = std::make_shared<FakeCache>();
    core::DiagramService service(renderer, cache);

    const core::contracts::DiagramSource source{"graph TD; A-->B;", "mermaid"};
    const core::contracts::RenderOptions options{"svg", 1.0f};

    const auto first = service.renderDiagram(source, options);
    const auto second = service.renderDiagram(source, options);

    CHECK(first.success);
    CHECK(second.success);
    CHECK(first.data == second.data);
    CHECK(renderer->render_calls == 1);
}

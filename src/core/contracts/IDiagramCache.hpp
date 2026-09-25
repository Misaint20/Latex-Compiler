#pragma once

#include <string>
#include <optional>

namespace core {
namespace contracts {

class IDiagramCache {
public:
    virtual ~IDiagramCache() = default;
    virtual std::optional<std::string> get(const std::string& hashKey) = 0;
    virtual void set(const std::string& hashKey, const std::string& renderedData) = 0;
};

} // namespace contracts
} // namespace core

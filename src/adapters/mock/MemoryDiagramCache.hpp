#pragma once

#include "core/contracts/IDiagramCache.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace adapters {
namespace mock {

class MemoryDiagramCache : public core::contracts::IDiagramCache {
public:
    std::optional<std::string> get(const std::string& hashKey) override {
        const auto found = cache_.find(hashKey);
        if (found == cache_.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    void set(const std::string& hashKey, const std::string& renderedData) override {
        cache_[hashKey] = renderedData;
    }

private:
    std::unordered_map<std::string, std::string> cache_;
};

} // namespace mock
} // namespace adapters

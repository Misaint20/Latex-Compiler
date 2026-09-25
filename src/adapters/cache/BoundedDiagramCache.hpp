#pragma once

#include "core/contracts/IDiagramCache.hpp"

#include <cstddef>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace adapters {
namespace cache {

// LRU cache with a total-byte budget. Rendered SVGs are the largest in-memory
// object in the app; an unbounded map kept every diagram ever viewed alive for
// the whole session. The budget keeps memory flat no matter how much the user
// browses.
class BoundedDiagramCache : public core::contracts::IDiagramCache {
public:
    explicit BoundedDiagramCache(std::size_t max_bytes = 24u * 1024u * 1024u)
        : max_bytes_(max_bytes) {}

    std::optional<std::string> get(const std::string& hashKey) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = entries_.find(hashKey);
        if (found == entries_.end()) {
            return std::nullopt;
        }
        lru_.splice(lru_.begin(), lru_, found->second.lru_it);
        return found->second.data;
    }

    void set(const std::string& hashKey, const std::string& renderedData) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = entries_.find(hashKey);
        if (found != entries_.end()) {
            current_bytes_ -= found->second.data.size();
            found->second.data = renderedData;
            current_bytes_ += renderedData.size();
            lru_.splice(lru_.begin(), lru_, found->second.lru_it);
        } else {
            lru_.push_front(hashKey);
            entries_.emplace(hashKey, Entry{renderedData, lru_.begin()});
            current_bytes_ += renderedData.size();
        }
        while (current_bytes_ > max_bytes_ && lru_.size() > 1) {
            const auto& oldest = lru_.back();
            const auto it = entries_.find(oldest);
            current_bytes_ -= it->second.data.size();
            entries_.erase(it);
            lru_.pop_back();
        }
    }

private:
    struct Entry {
        std::string data;
        std::list<std::string>::iterator lru_it;
    };

    std::size_t max_bytes_;
    std::size_t current_bytes_ = 0;
    std::list<std::string> lru_;
    std::unordered_map<std::string, Entry> entries_;
    std::mutex mutex_;
};

} // namespace cache
} // namespace adapters

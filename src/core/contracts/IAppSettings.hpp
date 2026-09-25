#pragma once

#include <string>

namespace core {
namespace contracts {

// Key/value application settings persisted by an adapter. Values are opaque
// strings; the core owns the key names and semantics.
class IAppSettings {
public:
    virtual ~IAppSettings() = default;

    virtual std::string get(const std::string& key, const std::string& fallback) = 0;
    virtual void set(const std::string& key, const std::string& value) = 0;
};

} // namespace contracts
} // namespace core

#pragma once

#include <string>
#include <vector>

namespace core {
namespace contracts {

class IFileStore {
public:
    virtual ~IFileStore() = default;
    virtual std::string readFile(const std::string& path) = 0;
    virtual void writeFile(const std::string& path, const std::string& content) = 0;
    virtual bool exists(const std::string& path) const = 0;
    virtual std::vector<std::string> listDirectory(const std::string& path) const = 0;
};

} // namespace contracts
} // namespace core

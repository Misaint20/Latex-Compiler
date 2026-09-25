#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core {
namespace contracts {

struct ProjectFile {
    std::string name;
    std::string folder;
    std::uintmax_t size_bytes = 0;
};

struct ProjectInfo {
    std::string projectPath;
    std::vector<std::string> texFiles;
    std::string mainFileCandidate;
    std::vector<ProjectFile> chapters;
    std::vector<ProjectFile> diagrams;
    std::vector<ProjectFile> styles;
    std::vector<ProjectFile> assets;
};

class IProjectScanner {
public:
    virtual ~IProjectScanner() = default;
    virtual ProjectInfo scan(const std::string& directoryPath) = 0;
};

} // namespace contracts
} // namespace core

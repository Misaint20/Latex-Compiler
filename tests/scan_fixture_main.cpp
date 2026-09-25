#include "adapters/filesystem/SystemProjectScanner.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: scan-fixture <dir>" << std::endl;
        return 2;
    }

    adapters::filesystem::SystemProjectScanner scanner;
    const auto info = scanner.scan(argv[1]);

    std::cout << "project: " << info.projectPath << "\n"
              << "main:    " << info.mainFileCandidate << "\n";
    for (const auto& f : info.chapters) {
        std::cout << "chapter: " << f.folder << "/" << f.name << "\n";
    }
    for (const auto& f : info.diagrams) {
        std::cout << "diagram: " << f.folder << "/" << f.name << "\n";
    }
    for (const auto& f : info.assets) {
        std::cout << "asset:   " << f.folder << "/" << f.name << "\n";
    }
    return 0;
}

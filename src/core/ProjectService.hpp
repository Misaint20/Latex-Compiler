#pragma once

#include "contracts/IProjectScanner.hpp"
#include "contracts/IRecentProjects.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace core
{
    namespace contracts
    {
        class IFolderPicker;
        class IFileStore;
        class IFileOpener;
    }
    class EditorService;

    class ProjectService
    {
    public:
        explicit ProjectService(std::shared_ptr<contracts::IProjectScanner> scanner);
        ProjectService(std::shared_ptr<contracts::IProjectScanner> scanner,
                       std::shared_ptr<contracts::IFolderPicker> folder_picker);
        ProjectService(std::shared_ptr<contracts::IProjectScanner> scanner,
                       std::shared_ptr<contracts::IFolderPicker> folder_picker,
                       std::shared_ptr<contracts::IFileStore> file_store);
        ProjectService(std::shared_ptr<contracts::IProjectScanner> scanner,
                       std::shared_ptr<contracts::IFolderPicker> folder_picker,
                       std::shared_ptr<contracts::IFileStore> file_store,
                       std::shared_ptr<contracts::IFileOpener> file_opener,
                       std::shared_ptr<contracts::IRecentProjects> recents = nullptr);

        contracts::ProjectInfo scanProject(const std::string &path);
        std::string pickProjectFolder(const std::string &start_path);

        // Recent projects, most recent first.
        std::vector<contracts::RecentProject> recentProjects();
        void recordRecentProject(const std::string &path);
        bool removeRecentProject(const std::string &path);
        // Drops recents whose folder no longer exists; returns the removed count.
        std::size_t removeMissingRecentProjects();
        // Same, but re-probes the filesystem ignoring any existence cache.
        std::size_t removeMissingRecentProjectsForceProbe();

        // Persists the user's main-file choice for a project.
        void rememberMainFile(const std::string &path, const std::string &main_file);

        // Persists the last active explorer tab for a project (no-op when the
        // project is not in the recents list).
        void rememberLastTab(const std::string &path, const std::string &tab);

        // Reads a text file that lives inside the project root. Returns an empty
        // optional when the file is missing or escapes the project root.
        std::optional<std::string> readProjectFile(const std::string &project_path,
                                                   const std::string &relative_path);

        // Reads a binary previewable asset (images, PDF) inside the project root
        // together with the MIME type guessed from the extension.
        struct BinaryFile
        {
            std::string bytes;
            std::string mime;
        };
        std::optional<BinaryFile> readProjectFileBytes(const std::string &project_path,
                                                       const std::string &relative_path);

        // Writes a diagram source file (.mmd/.mermaid) inside the project root.
        // Returns false when the path escapes the project root or the extension
        // is not a diagram source.
        bool writeTextFile(const std::string &project_path,
                              const std::string &relative_path,
                              const std::string &content);

        // Opens a project file with the OS default handler, or with the user's
        // preferred editor for LaTeX sources. Returns false when the file is
        // missing or escapes the project root.
        bool openProjectFile(const std::string &project_path, const std::string &relative_path);

        void setEditorService(std::shared_ptr<EditorService> editor_service);

    private:
        // Resolves a relative path with the containment guard; empty when invalid.
        std::optional<std::string> resolveInsideProject(const std::string &project_path,
                                                        const std::string &relative_path) const;

        std::shared_ptr<contracts::IProjectScanner> scanner_;
        std::shared_ptr<contracts::IFolderPicker> folder_picker_;
        std::shared_ptr<contracts::IFileStore> file_store_;
        std::shared_ptr<contracts::IFileOpener> file_opener_;
        std::shared_ptr<contracts::IRecentProjects> recents_;
        std::shared_ptr<EditorService> editor_service_;
    };

} // namespace core

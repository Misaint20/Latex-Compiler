#include "ProjectService.hpp"
#include "EditorService.hpp"
#include "ProjectPaths.hpp"
#include "contracts/IFileOpener.hpp"
#include "contracts/IFileStore.hpp"
#include "contracts/IFolderPicker.hpp"
#include "contracts/IRecentProjects.hpp"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <unordered_map>

namespace core
{

    ProjectService::ProjectService(std::shared_ptr<contracts::IProjectScanner> scanner)
        : ProjectService(std::move(scanner), {}, {}) {}

    ProjectService::ProjectService(std::shared_ptr<contracts::IProjectScanner> scanner,
                                   std::shared_ptr<contracts::IFolderPicker> folder_picker)
        : ProjectService(std::move(scanner), std::move(folder_picker), {}) {}

    ProjectService::ProjectService(std::shared_ptr<contracts::IProjectScanner> scanner,
                                   std::shared_ptr<contracts::IFolderPicker> folder_picker,
                                   std::shared_ptr<contracts::IFileStore> file_store)
        : ProjectService(std::move(scanner), std::move(folder_picker), std::move(file_store), {}) {}

    ProjectService::ProjectService(std::shared_ptr<contracts::IProjectScanner> scanner,
                                   std::shared_ptr<contracts::IFolderPicker> folder_picker,
                                   std::shared_ptr<contracts::IFileStore> file_store,
                                   std::shared_ptr<contracts::IFileOpener> file_opener,
                                   std::shared_ptr<contracts::IRecentProjects> recents)
        : scanner_(std::move(scanner)),
          folder_picker_(std::move(folder_picker)),
          file_store_(std::move(file_store)),
          file_opener_(std::move(file_opener)),
          recents_(std::move(recents)) {}

    contracts::ProjectInfo ProjectService::scanProject(const std::string &path)
    {
        auto info = scanner_->scan(path);
        if (!info.projectPath.empty() && !info.texFiles.empty() && recents_)
        {
            recents_->record(info.projectPath);
        }
        return info;
    }

    std::string ProjectService::pickProjectFolder(const std::string &start_path)
    {
        if (!folder_picker_)
        {
            return {};
        }
        auto folder = folder_picker_->pickFolder("Select a LaTeX project folder", start_path);
        return folder.value_or(std::string{});
    }

    std::optional<std::string> ProjectService::resolveInsideProject(
        const std::string &project_path, const std::string &relative_path) const
    {
        namespace fs = std::filesystem;

        if (project_path.empty() || relative_path.empty())
        {
            return std::nullopt;
        }

        std::error_code ec;
        const fs::path root = fs::weakly_canonical(fs::path(project_path), ec);
        if (ec || root.empty())
        {
            return std::nullopt;
        }

        fs::path target = fs::weakly_canonical(root / fs::path(relative_path), ec);
        if (ec)
        {
            return std::nullopt;
        }

        // Reject anything that escapes the project root via traversal or symlinks.
        const auto [root_it, target_it] = std::mismatch(
            root.begin(), root.end(), target.begin(), target.end());
        if (root_it != root.end())
        {
            return std::nullopt;
        }

        return target.generic_string();
    }

    std::optional<std::string> ProjectService::readProjectFile(
        const std::string &project_path, const std::string &relative_path)
    {
        if (!file_store_)
        {
            return std::nullopt;
        }
        const auto resolved = resolveInsideProject(project_path, relative_path);
        if (!resolved || !file_store_->exists(*resolved))
        {
            return std::nullopt;
        }
        try
        {
            return file_store_->readFile(*resolved);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<ProjectService::BinaryFile> ProjectService::readProjectFileBytes(
        const std::string &project_path, const std::string &relative_path)
    {
        if (!file_store_)
        {
            return std::nullopt;
        }
        const auto resolved = resolveInsideProject(project_path, relative_path);
        if (!resolved || !file_store_->exists(*resolved))
        {
            return std::nullopt;
        }
        static const std::unordered_map<std::string, std::string> kMimeByExtension = {
            {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},
            {".gif", "image/gif"}, {".webp", "image/webp"}, {".bmp", "image/bmp"},
            {".svg", "image/svg+xml"}, {".pdf", "application/pdf"},
        };
        const std::size_t dot = resolved->rfind('.');
        const std::string extension = dot == std::string::npos
                                          ? std::string{}
                                          : resolved->substr(dot);
        const auto mime_it = kMimeByExtension.find(extension);
        if (mime_it == kMimeByExtension.end())
        {
            return std::nullopt;
        }
        try
        {
            return BinaryFile{file_store_->readFile(*resolved), mime_it->second};
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool ProjectService::writeTextFile(const std::string &project_path,
                                          const std::string &relative_path,
                                          const std::string &content)
    {
        if (!file_store_)
        {
            return false;
        }
        const auto resolved = resolveInsideProject(project_path, relative_path);
        if (!resolved)
        {
            return false;
        }
        const std::size_t dot = resolved->rfind('.');
        if (dot == std::string::npos)
        {
            return false;
        }
        const std::string extension = resolved->substr(dot);
        static const char* kEditableExtensions[] = {".mmd", ".mermaid", ".tex"};
        const bool editable = std::any_of(std::begin(kEditableExtensions),
                                          std::end(kEditableExtensions),
                                          [&](const char* allowed) { return extension == allowed; });
        if (!editable)
        {
            return false;
        }
        try
        {
            file_store_->writeFile(*resolved, content);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ProjectService::openProjectFile(const std::string &project_path,
                                         const std::string &relative_path)
    {
        if (editor_service_)
        {
            return editor_service_->openInProject(project_path, relative_path);
        }
        if (!file_opener_)
        {
            return false;
        }
        const auto resolved = resolveInsideProject(project_path, relative_path);
        if (!resolved)
        {
            return false;
        }
        return file_opener_->open(*resolved);
    }

    void ProjectService::setEditorService(std::shared_ptr<EditorService> editor_service)
    {
        editor_service_ = std::move(editor_service);
    }

    std::vector<contracts::RecentProject> ProjectService::recentProjects()
    {
        if (!recents_)
        {
            return {};
        }
        return recents_->list();
    }

    void ProjectService::recordRecentProject(const std::string &path)
    {
        if (recents_)
        {
            recents_->record(path);
        }
    }

    bool ProjectService::removeRecentProject(const std::string &path)
    {
        if (!recents_)
        {
            return false;
        }
        return recents_->remove(path);
    }

    std::size_t ProjectService::removeMissingRecentProjects()
    {
        if (!recents_)
        {
            return 0;
        }
        return recents_->removeMissing();
    }

    std::size_t ProjectService::removeMissingRecentProjectsForceProbe()
    {
        if (!recents_)
        {
            return 0;
        }
        return recents_->removeMissingForceProbe();
    }

    void ProjectService::rememberMainFile(const std::string &path, const std::string &main_file)
    {
        if (recents_)
        {
            recents_->rememberMainFile(path, main_file);
        }
    }

    void ProjectService::rememberLastTab(const std::string &path, const std::string &tab)
    {
        if (recents_)
        {
            recents_->rememberLastTab(path, tab);
        }
    }

} // namespace core

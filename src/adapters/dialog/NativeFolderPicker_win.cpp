#include "adapters/dialog/NativeFolderPicker.hpp"

#include <shlobj.h>
#include <memory>
#include <vector>

namespace adapters {
namespace dialog {
namespace {

// IFileDialog must run on a thread with an initialized COM apartment. The
// dispatcher worker thread has none, so the dialog is self-contained: it
// initializes COM for this call and always uninitializes on scope exit.
class ComApartment {
public:
    ComApartment() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        init_result_ = hr;
        if (SUCCEEDED(hr) && hr != RPC_E_CHANGED_MODE) {
            must_uninit_ = true;
        }
    }
    ~ComApartment() {
        if (must_uninit_) {
            CoUninitialize();
        }
    }
    bool ok() const { return SUCCEEDED(init_result_); }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

private:
    HRESULT init_result_ = E_FAIL;
    bool must_uninit_ = false;
};

} // namespace

std::optional<std::string> NativeFolderPicker::pickFolder(const std::string& title,
                                                          const std::string& start_path) {
    ComApartment com;
    if (!com.ok()) {
        return std::nullopt;
    }
    IFileDialog* dialog_raw = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog_raw)))) {
        return std::nullopt;
    }
    std::unique_ptr<IFileDialog, decltype([](IFileDialog* d) { d->Release(); })> dialog(dialog_raw);

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(std::wstring(title.begin(), title.end()).c_str());

    if (!start_path.empty()) {
        std::wstring wide(start_path.begin(), start_path.end());
        IShellItem* folder_raw = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(wide.c_str(), nullptr,
                                                  IID_PPV_ARGS(&folder_raw)))) {
            dialog->SetFolder(folder_raw);
            folder_raw->Release();
        }
    }

    if (FAILED(dialog->Show(nullptr))) {
        return std::nullopt;
    }

    IShellItem* result_raw = nullptr;
    if (FAILED(dialog->GetResult(&result_raw))) {
        return std::nullopt;
    }
    std::unique_ptr<IShellItem, decltype([](IShellItem* item) { item->Release(); })> result(result_raw);

    PWSTR path_raw = nullptr;
    if (FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &path_raw))) {
        return std::nullopt;
    }
    std::string path(path_raw, path_raw + wcslen(path_raw));
    CoTaskMemFree(path_raw);
    return std::optional{path};
}

} // namespace dialog
} // namespace adapters

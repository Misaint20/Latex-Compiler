#include "adapters/dialog/NativeFolderPicker.hpp"

#include <shlobj.h>

#include <cwchar>
#include <memory>
#include <string>
#include <vector>
#include <optional>

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

// UTF-8 <-> UTF-16 through the real conversion APIs. Char-by-char copies
// between char and wchar_t (what MSVC warned about with C4244) truncate every
// non-ASCII byte, breaking paths with accents or other multibyte characters.
std::wstring wide_from_utf8(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(),
                        static_cast<int>(utf8.size()), wide.data(), size);
    wide.resize(static_cast<size_t>(size) - 1); // drop the terminating null
    return wide;
}

std::string utf8_from_wide(const wchar_t* wide) {
    if (wide == nullptr || *wide == L'\0') {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    // The reported size includes the terminating null: allocate for it, copy,
    // then drop it.
    std::string narrow(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, narrow.data(), size, nullptr, nullptr);
    narrow.resize(static_cast<size_t>(size) - 1);
    return narrow;
}

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
    dialog->SetTitle(wide_from_utf8(title).c_str());

    if (!start_path.empty()) {
        const std::wstring wide = wide_from_utf8(start_path);
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
    std::string path = utf8_from_wide(path_raw);
    CoTaskMemFree(path_raw);
    return std::optional{path};
}

} // namespace dialog
} // namespace adapters

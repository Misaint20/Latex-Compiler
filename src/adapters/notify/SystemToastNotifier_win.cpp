#include "adapters/notify/SystemToastNotifier.hpp"

#include <windows.h>

#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/base.h>

#include <cwchar>
#include <string>
#include <memory>
#include <utility>

namespace adapters {
namespace notify {
namespace {

// Toast content is XML: escape everything the user's titles may contain.
std::wstring xml_escape(const std::string& utf8) {
    // MultiByteToWideChar is stable Win32 across SDK generations (the C++/WinRT
    // to_hstring return type is not: newer Windows SDKs return hstring, which
    // no longer converts to std::wstring implicitly).
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
    // The query reports the terminating null as part of the size; drop it so
    // no NUL character leaks into the escaped XML.
    wide.resize(static_cast<size_t>(size) - 1);
    std::wstring out;
    out.reserve(wide.size() + 8);
    for (const wchar_t c : wide) {
        switch (c) {
        case L'&': out += L"&amp;"; break;
        case L'<': out += L"&lt;"; break;
        case L'>': out += L"&gt;"; break;
        case L'"': out += L"&quot;"; break;
        case L'\'': out += L"&apos;"; break;
        default: out += c;
        }
    }
    return out;
}

// Toast activation arguments live in the toast element's launch attribute
// (documented contract, stable across Windows versions — unlike the
// ToastNotification::Launch method, which disappeared from current SDK
// headers for unpackaged apps).
constexpr wchar_t kToastTemplate[] =
    L"<toast launch=\"{ARGS}\"><visual><binding template=\"ToastGeneric\">"
    L"<text>{TITLE}</text><text>{BODY}</text></binding></visual>"
    L"<audio src=\"ms-winsoundevent:Notification.Default\"/></toast>";

// COM apartments are thread-local; init once per worker thread, never throw.
bool ensure_apartment() {
    static thread_local const bool ready = [] {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            return true;
        } catch (...) {
            return false;
        }
    }();
    return ready;
}

} // namespace

SystemToastNotifier::SystemToastNotifier(
    std::shared_ptr<core::contracts::IProcessRunner> runner)
    : runner_(std::move(runner)) {}

void SystemToastNotifier::notify(const std::string& title, const std::string& body,
                                 const std::string& open_path) {
    if (!ensure_apartment()) {
        return;
    }
    try {
        std::wstring xml = kToastTemplate;
        const auto replace_first = [&xml](const wchar_t* token, const std::wstring& value) {
            const size_t at = xml.find(token);
            if (at != std::wstring::npos) {
                xml.replace(at, wcslen(token), value);
            }
        };
        replace_first(L"{TITLE}", xml_escape(title));
        replace_first(L"{BODY}", xml_escape(body));
        if (!open_path.empty()) {
            // Delivered to a registered toast activator; harmless otherwise.
            replace_first(L"{ARGS}", L"open=" + xml_escape(open_path));
        } else {
            replace_first(L"{ARGS}", L"");
        }
        winrt::Windows::Data::Xml::Dom::XmlDocument document;
        document.LoadXml(xml);

        auto content =
            winrt::Windows::UI::Notifications::ToastNotification(document);

        // The AUMID must match the app's Start-menu shortcut for reliable
        // delivery from unpackaged builds (standard packaging step).
        auto notifier = winrt::Windows::UI::Notifications::ToastNotificationManager::
            CreateToastNotifier(L"Misaint20.latexcompiler");
        notifier.Show(content);
    } catch (...) {
        // No WinRT desktop support or no registered AUMID: toasting is
        // best-effort by contract and must never break a compile.
    }
}

} // namespace notify
} // namespace adapters

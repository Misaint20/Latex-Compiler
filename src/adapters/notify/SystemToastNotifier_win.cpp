#include "adapters/notify/SystemToastNotifier.hpp"

#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/base.h>

#include <string>

namespace adapters {
namespace notify {
namespace {

// Toast content is XML: escape everything the user's titles may contain.
std::wstring xml_escape(const std::string& utf8) {
    std::wstring out;
    const std::wstring wide = winrt::to_hstring(utf8);
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
        std::wstring xml = L"<toast><visual><binding template=\"ToastGeneric\"><text>" +
                           xml_escape(title) + L"</text><text>" + xml_escape(body) +
                           L"</text></binding></visual>"
                           L"<audio src=\"ms-winsoundevent:Notification.Default\"/></toast>";
        winrt::Windows::Data::Xml::Dom::XmlDocument document;
        document.LoadXml(xml);

        auto content = winrt::Windows::UI::Notifications::ToastNotification(document);
        if (!open_path.empty()) {
            // Delivered to a registered toast activator; harmless otherwise.
            content.Launch(winrt::to_hstring(open_path));
        }

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

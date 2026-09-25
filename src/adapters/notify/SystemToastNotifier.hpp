#pragma once

#include "core/contracts/INotifier.hpp"

#include <memory>
#include <string>

namespace core {
namespace contracts {
class IProcessRunner;
}
}

namespace adapters {
namespace notify {

// Native toasts on Windows (ToastNotificationManager via WinRT) and Linux
// (notify-send/libnotify) behind the same INotifier port as macOS. `runner`
// is the spawn channel for notify-send on Linux; Windows talks to WinRT
// directly and keeps the runner unused. Fire-and-forget by contract.
//
// Click behaviour follows each platform's model: macOS opens open_path via
// NSWorkspace; on Windows the toast carries a launch argument (honoured once
// the app registers a toast activator) and on Linux libnotify has no
// click-activation without a handler daemon, so there it only informs.
class SystemToastNotifier : public core::contracts::INotifier {
public:
    explicit SystemToastNotifier(std::shared_ptr<core::contracts::IProcessRunner> runner);

    void notify(const std::string& title, const std::string& body,
                const std::string& open_path) override;

private:
    std::shared_ptr<core::contracts::IProcessRunner> runner_;
};

} // namespace notify
} // namespace adapters

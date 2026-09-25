#pragma once

#include "core/contracts/INotifier.hpp"

namespace adapters {
namespace notify {

// macOS Notification Center delivery through UNUserNotificationCenter.
// Stays silent while the app is frontmost (the window already shows the
// result), so notifications only appear when the user has switched away.
// All AppKit/UserNotifications work is marshalled to the main queue.
class MacSystemNotifier : public core::contracts::INotifier {
public:
    void notify(const std::string& title, const std::string& body, const std::string& open_path) override;
};

} // namespace notify
} // namespace adapters

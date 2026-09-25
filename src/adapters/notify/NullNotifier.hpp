#pragma once

#include "core/contracts/INotifier.hpp"

namespace adapters {
namespace notify {

// No-op notifier for platforms without a native implementation and for
// tests that do not care about notifications.
class NullNotifier : public core::contracts::INotifier {
public:
    void notify(const std::string&, const std::string&, const std::string&) override {}
};

} // namespace notify
} // namespace adapters

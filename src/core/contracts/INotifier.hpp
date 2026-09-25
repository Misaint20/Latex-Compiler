#pragma once

#include <string>

namespace core {
namespace contracts {

// OS-level user notifications (e.g. Notification Center) so the user learns
// a finished compile without keeping the app focused. Implementations decide
// themselves when to stay silent (e.g. the app is already frontmost).
class INotifier {
public:
    virtual ~INotifier() = default;

    // title: short heading; body: one-line detail. open_path: a file the OS
    // should reveal when the user clicks the notification (empty = nothing
    // to open). Fire-and-forget; must never block or fail the calling job.
    virtual void notify(const std::string& title, const std::string& body,
                        const std::string& open_path) = 0;
};

} // namespace contracts
} // namespace core

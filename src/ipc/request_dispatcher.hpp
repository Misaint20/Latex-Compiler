#pragma once

#include "core/CompileService.hpp"
#include "core/EditorService.hpp"
#include "core/ProjectService.hpp"
#include "core/DiagramService.hpp"
#include "core/contracts/IAppSettings.hpp"
#include "ipc/TaskExecutor.hpp"
#include <functional>
#include <string>
#include <string_view>
#include <memory>
#include <utility>

namespace ipc {

class RequestDispatcher;

// Declared early so the handler methods can throw it.
class IpcError {
public:
    IpcError(std::string id, std::string code, std::string message)
        : id(std::move(id)), code(std::move(code)), message(std::move(message)) {}
    std::string id;
    std::string code;
    std::string message;
};

// Validation failure before an id is available.
struct ParseIdError {};

class RequestDispatcher {
public:
    RequestDispatcher(
        std::shared_ptr<core::CompileService> compileService,
        std::shared_ptr<core::ProjectService> projectService,
        std::shared_ptr<core::DiagramService> diagramService,
        std::shared_ptr<core::EditorService> editorService = nullptr,
        std::shared_ptr<core::contracts::IAppSettings> appSettings = nullptr
    );
    std::string dispatch(std::string_view raw_request);

    // Same as above, but remembers the webview binding id so slow-handler
    // replies can resolve the frontend Promise through the reply sink.
    std::string dispatch(std::string_view raw_request, std::string binding_id);

    // Pushes stored user preferences (notification toggle, ...) into the
    // services; called once at composition time, before the window loads.
    void applyStoredPreferences();

    // Destination for replies produced off the dispatch thread (slow-handler
    // path). The platform window installs one built on the webview async
    // binding; tests install a queue. Call before the first dispatch.
    void setReplySink(std::function<void(std::string, std::string)> sink) {
        reply_sink_ = std::move(sink);
    }

    // Destination for replies produced on the dispatch thread (fast handlers).
    // The async webview binding discards the dispatch() return value, so the
    // platform window uses this to resolve frontend Promises; with no sink or
    // no binding id (tests), responses still come back as the return value.
    void setSyncReplySink(std::function<void(std::string, std::string)> sink) {
        sync_reply_sink_ = std::move(sink);
    }

    // Native project-menu toggle (Apple menu bar only); no-op elsewhere.
    void setProjectMenuEnabler(std::function<void(bool)> enabler) {
        project_menu_enabler_ = std::move(enabler);
    }

    // Retitles the native menu bar to the app language; no-op elsewhere.
    void setNativeMenuLanguageSetter(std::function<void(const std::string&)> setter) {
        native_menu_language_setter_ = std::move(setter);
    }

private:
    // Serializes a request into the id/method/params triple the handlers and
    // the async wrapper need; valid for the lifetime of the returned strings.
    struct RequestParts
    {
        std::string id;
        std::string method;
        std::string params_json;
        std::string binding_id;
        int version = 0;
    };
    static RequestParts split_request(std::string_view raw_request);

    void submitReply(std::string binding_id, std::string response_json);
    std::string dispatchSync(RequestParts parts);

    // Slow handlers; declared to return the serialized response JSON so the
    // header does not need the nlohmann include. Defined in the .cpp where
    // json is in scope.
    struct HandlerResult
    {
        std::string response_json;
    };
    HandlerResult handlePickFolder(const std::string& params_json, std::string& id);
    HandlerResult handleReadFile(const std::string& params_json, std::string& id);
    HandlerResult handleReadBinary(const std::string& params_json, std::string& id);

    std::shared_ptr<core::CompileService> compileService_;
    std::shared_ptr<core::ProjectService> projectService_;
    std::shared_ptr<core::DiagramService> diagramService_;
    std::shared_ptr<core::EditorService> editorService_;
    std::shared_ptr<core::contracts::IAppSettings> appSettings_;

    // Executes slow handlers (scans, binary reads, history) off the caller's
    // thread; the wrapper returns completion via the webview async binding.
    TaskExecutor executor_;

    std::function<void(std::string, std::string)> reply_sink_;
    std::function<void(std::string, std::string)> sync_reply_sink_;
    std::function<void(bool)> project_menu_enabler_ = [](bool) {};
    std::function<void(const std::string&)> native_menu_language_setter_ =
        [](const std::string&) {};

    static constexpr const char* kLanguageKey = "app.language";
    static constexpr const char* kNotificationsKey = "app.notifications";
};

} // namespace ipc

#include "ipc/request_dispatcher.hpp"
#include "ipc/protocol.hpp"
#include <nlohmann/json.hpp>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <cstddef>
#include <memory>
#include <vector>

namespace {
using json = nlohmann::json;

std::string error_response(std::string id, std::string code, std::string message) {
    return json{
        {"version", ipc::protocol_version},
        {"id", std::move(id)},
        {"ok", false},
        {"error", {{"code", std::move(code)}, {"message", std::move(message)}}},
    }.dump();
}

json file_to_json(const core::contracts::ProjectFile& file) {
    return json{
        {"name", file.name},
        {"folder", file.folder},
        {"sizeBytes", file.size_bytes},
    };
}

json recent_to_json(const core::contracts::RecentProject& project) {
    return json{
        {"path", project.path},
        {"lastOpenedMs", project.last_opened_ms},
        {"exists", project.exists},
        {"lastTab", project.last_tab},
    };
}

json history_to_json(const core::contracts::HistoryEntry& entry) {
    json durations = json::object();
    for (const auto& [key, ms] : entry.stage_durations_ms) {
        durations[key] = ms;
    }
    return json{
        {"timestampMs", entry.timestamp_ms},
        {"durationMs", entry.duration_ms},
        {"result", entry.result},
        {"mainFile", entry.main_file},
        {"outputPath", entry.output_path},
        {"errorMessage", entry.error_message},
        {"stagesReached", entry.stages_reached},
        {"stageDurationsMs", durations},
    };
}

const char* state_to_string(core::CompileService::JobState state) {
    switch (state) {
        case core::CompileService::JobState::Idle: return "idle";
        case core::CompileService::JobState::Running: return "running";
        case core::CompileService::JobState::Succeeded: return "succeeded";
        case core::CompileService::JobState::Failed: return "failed";
        case core::CompileService::JobState::Canceled: return "canceled";
    }
    return "idle";
}

} // namespace

namespace ipc {
namespace {

json editor_preset_to_json(const core::contracts::EditorPreset& preset) {
    return json{
        {"id", preset.id},
        {"label", preset.label},
        {"installed", preset.installed},
    };
}

} // namespace

RequestDispatcher::RequestDispatcher(
    std::shared_ptr<core::CompileService> compileService,
    std::shared_ptr<core::ProjectService> projectService,
    std::shared_ptr<core::DiagramService> diagramService,
    std::shared_ptr<core::EditorService> editorService,
    std::shared_ptr<core::contracts::IAppSettings> appSettings)
    : compileService_(std::move(compileService)),
      projectService_(std::move(projectService)),
      diagramService_(std::move(diagramService)),
      editorService_(std::move(editorService)),
      appSettings_(std::move(appSettings)) {
}

void RequestDispatcher::applyStoredPreferences() {
    if (compileService_ && appSettings_) {
        compileService_->setNotificationsEnabled(
            appSettings_->get(kNotificationsKey, "1") == "1");
    }
}

// ---- Slow handlers, run on the executor thread ----
//
// Each handler receives the split request and returns the full response JSON.
// The dispatch wrapper moves them onto the TaskExecutor so the caller's thread
// (the UI main queue) never blocks on disk or dialogs.

RequestDispatcher::HandlerResult RequestDispatcher::handlePickFolder(const std::string& params_json, std::string& id) {
    const auto params = json::parse(params_json);
    const auto start = params.value("startPath", "");
    auto folder = projectService_->pickProjectFolder(start);
    return {json{
        {"version", protocol_version},
        {"id", id},
        {"ok", true},
        {"result", {{"path", std::move(folder)}}},
    }.dump()};
}

RequestDispatcher::HandlerResult RequestDispatcher::handleReadFile(const std::string& params_json, std::string& id) {
    const auto params = json::parse(params_json);
    const auto project_path = params.value("projectPath", "");
    const auto relative_path = params.value("relativePath", "");
    auto content = projectService_->readProjectFile(project_path, relative_path);
    if (!content) {
        throw IpcError(id, "file_not_found", "File is missing or outside the project root.");
    }
    return {json{
        {"version", protocol_version},
        {"id", id},
        {"ok", true},
        {"result", {{"content", std::move(*content)}}},
    }.dump()};
}

RequestDispatcher::HandlerResult RequestDispatcher::handleReadBinary(const std::string& params_json, std::string& id) {
    const auto params = json::parse(params_json);
    const auto project_path = params.value("projectPath", "");
    const auto relative_path = params.value("relativePath", "");
    auto file = projectService_->readProjectFileBytes(project_path, relative_path);
    if (!file) {
        throw IpcError(id, "file_not_found",
                       "Previewable asset is missing or outside the project root.");
    }
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((file->bytes.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 2 < file->bytes.size()) {
        const auto triple = (static_cast<unsigned char>(file->bytes[i]) << 16) |
                            (static_cast<unsigned char>(file->bytes[i + 1]) << 8) |
                            static_cast<unsigned char>(file->bytes[i + 2]);
        encoded.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        encoded.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        encoded.push_back(kAlphabet[(triple >> 6) & 0x3F]);
        encoded.push_back(kAlphabet[triple & 0x3F]);
        i += 3;
    }
    if (i + 1 == file->bytes.size()) {
        const auto triple = static_cast<unsigned char>(file->bytes[i]) << 16;
        encoded.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        encoded.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        encoded.append("==");
    } else if (i + 2 == file->bytes.size()) {
        const auto triple = (static_cast<unsigned char>(file->bytes[i]) << 8) |
                            static_cast<unsigned char>(file->bytes[i + 1]);
        encoded.push_back(kAlphabet[(triple >> 10) & 0x3F]);
        encoded.push_back(kAlphabet[(triple >> 4) & 0x3F]);
        encoded.push_back(kAlphabet[(triple << 2) & 0x3F]);
        encoded.push_back('=');
    }
    return {json{
        {"version", protocol_version},
        {"id", id},
        {"ok", true},
        {"result", {
            {"mime", std::move(file->mime)},
            {"dataBase64", std::move(encoded)},
        }},
    }.dump()};
}

RequestDispatcher::RequestParts RequestDispatcher::split_request(std::string_view raw_request) {
    const auto arguments = json::parse(raw_request);
    if (!arguments.is_array() || arguments.size() != 1 || !arguments.front().is_object()) {
        throw ParseIdError{};
    }
    const auto& request = arguments.front();
    RequestParts parts;
    parts.id = request.value("id", "");
    parts.method = request.value("method", "");
    parts.version = request.value("version", 0);
    if (request.contains("params") && request["params"].is_object()) {
        parts.params_json = request["params"].dump();
    }
    return parts;
}

std::string RequestDispatcher::dispatch(std::string_view raw_request) {
    return dispatch(raw_request, {});
}

std::string RequestDispatcher::dispatch(std::string_view raw_request, std::string binding_id) {
    // Split first: validation errors echo the id when one was sent.
    RequestParts parts;
    try {
        parts = split_request(raw_request);
    } catch (const ParseIdError&) {
        return error_response({}, "invalid_request", "Expected exactly one request object.");
    } catch (const json::exception&) {
        return error_response({}, "invalid_json", "Could not parse the IPC request.");
    }
    parts.binding_id = std::move(binding_id);
    if (parts.id.empty()) {
        const std::string response =
            error_response({}, "invalid_request", "Request id and method are required.");
        // Nothing references this id on the frontend; still surface it to
        // the log sink so bad callers are visible.
        if (sync_reply_sink_) {
            sync_reply_sink_({}, response);
        }
        return response;
    }

    // Slow handlers: move the work to the executor thread and finish the
    // request there. The frontend sees the same JSON responses as before; the
    // reply goes through the async-reply sink instead of the return value.
    if (parts.method == "project.pickFolder" || parts.method == "project.readFile" ||
        parts.method == "project.readProjectFileBinary") {
        // Queue-full must answer the frontend, never leave the Promise hung.
        // Handled inline below; the job moves parts and clears the local.
        auto job = std::make_shared<RequestParts>(std::move(parts));
        const bool accepted = executor_.try_submit([this, job]() {
            try {
                std::string response;
                if (job->method == "project.pickFolder") {
                    response = handlePickFolder(job->params_json, job->id).response_json;
                } else if (job->method == "project.readFile") {
                    response = handleReadFile(job->params_json, job->id).response_json;
                } else {
                    response = handleReadBinary(job->params_json, job->id).response_json;
                }
                submitReply(job->binding_id, std::move(response));
            } catch (const IpcError& e) {
                submitReply(job->binding_id,
                            error_response(e.id, e.code, e.message));
            } catch (const std::exception&) {
                submitReply(job->binding_id,
                            error_response(job->id, "internal_error", "Handler failed."));
            }
        });
        if (!accepted) {
            submitReply(job->binding_id,
                        error_response(job->id, "busy",
                                       "Too many pending requests; retry shortly."));
        }
        return "";
    }

    // Fast handlers: dispatchSync returns the serialized response, but the
    // async binding's callback return value is discarded by the library, so
    // the reply reaches the frontend only through the sync sink. (Tests call
    // dispatch() without a binding id and read the return value instead.)
    std::string sink_id = std::move(parts.binding_id);
    std::string response = dispatchSync(std::move(parts));
    if (!sink_id.empty() && sync_reply_sink_) {
        sync_reply_sink_(std::move(sink_id), response);
    }
    return response;
}

void RequestDispatcher::submitReply(std::string binding_id, std::string response_json) {
    if (reply_sink_) {
        reply_sink_(std::move(binding_id), std::move(response_json));
    }
}

std::string RequestDispatcher::dispatchSync(RequestParts parts) {
    const std::string& id = parts.id;
    const auto& request_method = parts.method;
    (void)request_method;
    if (parts.version != protocol_version) {
        return error_response(id, "unsupported_version", "Unsupported IPC protocol version.");
    }
    try {
        const auto params = parts.params_json.empty() ? json::object() : json::parse(parts.params_json);
        const auto request = json{{"method", parts.method}, {"params", params}};

        if (request["method"] == "app.ping") {
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"message", "pong"}}},
            }.dump();
        }

        if (request["method"] == "app.clientError") {
            const auto message = request["params"].value("message", "");
            const auto detail = request["params"].value("detail", "");
            std::cerr << "[frontend] " << message;
            if (!detail.empty()) {
                std::cerr << " | " << detail;
            }
            std::cerr << std::endl;
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"logged", true}}},
            }.dump();
        }

        if (request["method"] == "project.writeProjectFile") {
            const auto project_path = request["params"].value("projectPath", "");
            const auto relative_path = request["params"].value("relativePath", "");
            const auto content = request["params"].value("content", "");
            const bool saved = projectService_->writeTextFile(project_path, relative_path, content);
            if (!saved) {
                return error_response(id, "write_failed",
                                      "Could not save the diagram (missing store, traversal, or extension).");
            }
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"saved", true}}},
            }.dump();
        }

        if (request["method"] == "project.scan") {
            std::string path = request["params"].value("projectPath", "");
            auto info = projectService_->scanProject(path);

            // Surface the remembered main-file choice, if any, so the UI can
            // preselect it instead of the automatic candidate.
            std::string saved_main_file;
            std::string saved_last_tab;
            for (const auto& project : projectService_->recentProjects()) {
                if (project.path == info.projectPath) {
                    saved_main_file = project.main_file;
                    saved_last_tab = project.last_tab;
                    break;
                }
            }

            json chapters = json::array();
            json diagrams = json::array();
            json styles = json::array();
            json assets = json::array();
            for (const auto& file : info.chapters) {
                chapters.push_back(file_to_json(file));
            }
            for (const auto& file : info.diagrams) {
                diagrams.push_back(file_to_json(file));
            }
            for (const auto& file : info.styles) {
                styles.push_back(file_to_json(file));
            }
            for (const auto& file : info.assets) {
                assets.push_back(file_to_json(file));
            }

            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {
                    {"projectPath", info.projectPath},
                    {"texFiles", info.texFiles},
                    {"mainFileCandidate", info.mainFileCandidate},
                    {"savedMainFile", saved_main_file},
                    {"savedLastTab", saved_last_tab},
                    {"chapters", chapters},
                    {"diagrams", diagrams},
                    {"styles", styles},
                    {"assets", assets},
                }},
            }.dump();
        }

        if (request["method"] == "project.rememberLastTab") {
            const auto path = request["params"].value("projectPath", "");
            const auto tab = request["params"].value("tab", "");
            projectService_->rememberLastTab(path, tab);
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"saved", true}}},
            }.dump();
        }

        if (request["method"] == "project.rememberMainFile") {
            const auto path = request["params"].value("projectPath", "");
            const auto main_file = request["params"].value("mainFile", "");
            projectService_->rememberMainFile(path, main_file);
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"saved", true}}},
            }.dump();
        }

        if (request["method"] == "compiler.engines") {
            json engines = json::array();
            for (const auto& engine : compileService_->availableEngines()) {
                engines.push_back({
                    {"id", engine.id},
                    {"title", engine.title},
                    {"installed", engine.available},
                });
            }
            const std::string preferred = compileService_->preferredEngine();
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {
                    {"engines", engines},
                    {"preferred", preferred},
                    {"fallback", "tectonic"},
                }},
            }.dump();
        }

        if (request["method"] == "compiler.setEngine") {
            auto preferred = request["params"].value("engine", std::string{});
            if (preferred != "auto" && !compileService_->isKnownEngine(preferred)) {
                return error_response(id, "unknown_engine", "Unknown engine id.");
            }
            compileService_->setPreferredEngine(preferred);
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"saved", true}}},
            }.dump();
        }

        if (request["method"] == "compiler.start") {
            core::contracts::CompileRequest req{
                request["params"].value("projectPath", ""),
                request["params"].value("mainFile", "main.tex")
            };
            const bool started = compileService_->startCompile(req);
            if (!started) {
                return error_response(id, "compile_in_progress", "A compilation is already running.");
            }
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"started", true}}},
            }.dump();
        }

        if (request["method"] == "compiler.status") {
            const auto status = compileService_->jobStatus();
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {
                    {"state", state_to_string(status.state)},
                    {"progress", status.progress_percent},
                    {"message", status.message},
                    {"stage", status.stage},
                    {"stageKey", status.stage_key},
                    {"stagesReached", status.stages_reached},
                    {"failedStageKey", status.failed_stage_key},
                    {"stageDurationsMs", status.stage_durations_ms},
                    {"currentFile", status.current_file},
                    {"outputPath", status.output_path},
                    {"logTail", status.log_tail},
                    {"notice", status.notice},
                }},
            }.dump();
        }

        if (request["method"] == "compiler.cancel") {
            const bool canceled = compileService_->cancelActive();
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"canceled", canceled}}},
            }.dump();
        }

        if (request["method"] == "compiler.openOutput") {
            const bool opened = compileService_->openLastOutput();
            if (!opened) {
                return error_response(id, "open_failed",
                                      "No compiled PDF is available to open.");
            }
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"opened", true}}},
            }.dump();
        }

        if (request["method"] == "project.openFile") {
            const auto project_path = request["params"].value("projectPath", "");
            const auto relative_path = request["params"].value("relativePath", "");
            const bool opened = projectService_->openProjectFile(project_path, relative_path);
            if (!opened) {
                return error_response(id, "open_failed",
                                      "File is missing or outside the project root.");
            }
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"opened", true}}},
            }.dump();
        }

        if (request["method"] == "project.openFileWith") {
            // Hidden diagnostics-style method backing the per-diagram
            // open-with menu: launches with an explicit preset without
            // touching the stored preference. Empty preset = system default.
            const auto project_path = request["params"].value("projectPath", "");
            const auto relative_path = request["params"].value("relativePath", "");
            const auto preset_id = request["params"].value("preset", "");
            if (!editorService_) {
                return error_response(id, "unavailable", "Editor service unavailable.");
            }
            const bool opened = editorService_->openWithPreset(project_path, relative_path, preset_id);
            if (!opened) {
                return error_response(id, "open_failed",
                                      "File is missing or outside the project root.");
            }
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"opened", true}}},
            }.dump();
        }

        if (request["method"] == "editor.presets") {
            std::vector<core::contracts::EditorPreset> presets;
            bool availability = true;
            if (editorService_) {
                presets = editorService_->presets();
                availability = editorService_->available();
            }
            json items = json::array();
            for (const auto& preset : presets) {
                if (preset.id != "custom") {
                    items.push_back(editor_preset_to_json(preset));
                }
            }
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"presets", items}, {"availability", availability}}},
            }.dump();
        }

        // Hidden diagnostics method: not used by the UI; for tests and manual
        // refresh. Drops the presets TTL cache so the next editor.presets
        // re-probes the disk synchronously.
        if (request["method"] == "editor.invalidatePresetsCache") {
            if (!editorService_) {
                return error_response(id, "unavailable", "Editor service unavailable.");
            }
            editorService_->invalidatePresetsCache();
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"invalidated", true}}},
            }.dump();
        }

        if (request["method"] == "app.setProjectMenuEnabled") {
            project_menu_enabler_(request["params"].value("enabled", false));
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"applied", true}}},
            }.dump();
        }

        if (request["method"] == "app.getLanguage") {
            std::string language;
            if (appSettings_) {
                language = appSettings_->get(kLanguageKey, "");
            }
                native_menu_language_setter_(language);
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"language", language.empty() ? json(nullptr) : json(language)}}},
            }.dump();
        }

        if (request["method"] == "app.getNotifications") {
            bool enabled = true;
            if (appSettings_) {
                enabled = appSettings_->get(kNotificationsKey, "1") == "1";
            }
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"enabled", enabled}}},
            }.dump();
        }

        if (request["method"] == "app.setNotifications") {
            if (!appSettings_) {
                return error_response(id, "unavailable", "Settings storage unavailable.");
            }
            appSettings_->set(kNotificationsKey,
                              request["params"].value("enabled", true) ? "1" : "0");
            compileService_->setNotificationsEnabled(
                request["params"].value("enabled", true));
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"saved", true}}},
            }.dump();
        }

        if (request["method"] == "app.setLanguage") {
            if (!appSettings_) {
                return error_response(id, "unavailable", "Settings storage unavailable.");
            }
            const auto language = request["params"].value("language", "");
            if (!language.empty()) {
                appSettings_->set(kLanguageKey, language);
                native_menu_language_setter_(language);
            }
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"saved", true}}},
            }.dump();
        }

        if (request["method"] == "editor.get") {
            const bool diagram = request["params"].value("scope", "") == "diagram";
            const auto scope = diagram ? core::EditorService::Scope::Diagram
                                       : core::EditorService::Scope::Editor;
            std::string preferred;
            std::string custom_template;
            if (editorService_) {
                preferred = editorService_->preferred(scope);
                custom_template = editorService_->customTemplate(scope);
            }
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {
                    {"preferred", preferred},
                    {"customTemplate", custom_template},
                }},
            }.dump();
        }

        if (request["method"] == "editor.set") {
            if (!editorService_) {
                return error_response(id, "unavailable", "Editor service is not available.");
            }
            const bool diagram = request["params"].value("scope", "") == "diagram";
            const auto scope = diagram ? core::EditorService::Scope::Diagram
                                       : core::EditorService::Scope::Editor;
            editorService_->setPreferred(
                scope,
                request["params"].value("preferred", ""),
                request["params"].value("customTemplate", ""));
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"saved", true}}},
            }.dump();
        }

        if (request["method"] == "project.recentProjects") {
            auto projects = projectService_->recentProjects();
            json recents = json::array();
            for (const auto& project : projects) {
                recents.push_back(recent_to_json(project));
            }
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"projects", recents}}},
            }.dump();
        }

        if (request["method"] == "project.removeRecent") {
            const auto path = request["params"].value("path", "");
            const bool removed = projectService_->removeRecentProject(path);
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"removed", removed}}},
            }.dump();
        }

        if (request["method"] == "project.removeMissingRecents") {
            // Purging is an explicit user action: always re-probe the disk so
            // the count reflects reality, not a cached verdict.
            const auto removed = projectService_->removeMissingRecentProjectsForceProbe();
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"removed", removed}}},
            }.dump();
        }

        if (request["method"] == "project.recordRecent") {
            const auto path = request["params"].value("path", "");
            projectService_->recordRecentProject(path);
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"recorded", true}}},
            }.dump();
        }

        if (request["method"] == "compiler.history") {
            const auto project_path = request["params"].value("projectPath", "");
            auto entries = compileService_->history(project_path);

            json history = json::array();
            for (const auto& entry : entries) {
                history.push_back(history_to_json(entry));
            }
            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {{"entries", history}}},
            }.dump();
        }

        if (request["method"] == "compiler.compile") {
            core::contracts::CompileRequest req{
                request["params"].value("projectPath", ""),
                request["params"].value("mainFile", "main.tex")
            };
            auto result = compileService_->compileProject(req);

            if (!result.success) {
                return error_response(id, "compile_failed",
                                      result.errorMessage.empty() ? "Compilation failed."
                                                                  : result.errorMessage);
            }

            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {
                    {"success", result.success},
                    {"outputPath", result.outputPath},
                    {"errorMessage", result.errorMessage}
                }},
            }.dump();
        }

        if (request["method"] == "diagram.render") {
            core::contracts::DiagramSource source{
                request["params"].value("code", ""),
                request["params"].value("type", "mermaid")
            };
            core::contracts::RenderOptions options{
                request["params"].value("format", "svg"),
                request["params"].value("scale", 1.0f)
            };

            auto result = diagramService_->renderDiagram(source, options);

            if (!result.success) {
                return error_response(id, "render_failed",
                                      result.errorMessage.empty() ? "Diagram rendering failed."
                                                                  : result.errorMessage);
            }

            return json{
                {"version", protocol_version},
                {"id", id},
                {"ok", true},
                {"result", {
                    {"success", result.success},
                    {"data", result.data},
                    {"errorMessage", result.errorMessage}
                }},
            }.dump();
        }

        return error_response(
            id, "unknown_method",
            "Method not supported by this backend build: " + request["method"].get<std::string>());
    } catch (const std::exception&) {
        return error_response(id, "internal_error", "Handler failed.");
    }
}

} // namespace ipc

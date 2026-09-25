#pragma once

#include "core/contracts/IEventPublisher.hpp"

#include <deque>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace adapters {
namespace webview {

// Publishes core events into the webview by evaluating JS on the UI thread.
// The evaluate function must be safe to call from any thread; the platform
// window provides one built on webview::dispatch. Events are buffered until
// the frontend signals readiness via window.__nativeEventsReady.
class WebViewEventPublisher : public core::contracts::IEventPublisher {
public:
    using EvaluateFn = std::function<void(const std::string& js)>;

    explicit WebViewEventPublisher(EvaluateFn evaluate)
        : evaluate_(std::move(evaluate)) {}

    void publish(const core::contracts::Event& event) override {
        std::string js = "window.__emitNativeEvent && window.__emitNativeEvent(" +
                         quoted(event.topic) + ", " + quoted(event.payload) + ");";
        std::deque<std::string> batch;
        {
            std::lock_guard<std::mutex> lock(ready_mutex_);
            if (!ready_ || !evaluate_) {
                if (pending_.size() < 500) {
                    pending_.push_back(std::move(js));
                }
                return;
            }
            // Coalesce on the producer side: while a flush is in flight the
            // queue absorbs events and one dispatch delivers them all. This
            // caps main-thread wakeups when the compiler streams output.
            if (flush_in_flight_) {
                if (queued_.size() < 1000) {
                    queued_.push_back(std::move(js));
                }
                return;
            }
            flush_in_flight_ = true;
        }
        drain(batch);
    }

    void setReady(bool ready) {
        std::deque<std::string> flush;
        {
            std::lock_guard<std::mutex> lock(ready_mutex_);
            ready_ = ready;
            if (ready) {
                flush.swap(pending_);
            }
        }
        if (evaluate_) {
            for (const auto& js : flush) {
                evaluate_(js);
            }
        }
    }

private:
    // Sends one concatenated batch and re-arms the coalescer. Runs on the
    // caller of publish; the next publish after completion flushes again.
    void drain(std::deque<std::string>& batch) {
        if (evaluate_) {
            std::string combined;
            {
                std::lock_guard<std::mutex> lock(ready_mutex_);
                combined.reserve(queued_.size() * 128 + 128);
                while (!queued_.empty()) {
                    combined += queued_.front();
                    queued_.pop_front();
                }
            }
            evaluate_(combined + batchJs(batch));
        }
        {
            std::lock_guard<std::mutex> lock(ready_mutex_);
            flush_in_flight_ = false;
        }
    }

    static std::string batchJs(const std::deque<std::string>& batch) {
        std::string combined;
        for (const auto& js : batch) {
            combined += js;
        }
        return combined;
    }
    static std::string quoted(const std::string& raw) {
        std::string out = "\"";
        for (const unsigned char c : raw) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char buffer[7];
                        std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                        out += buffer;
                    } else {
                        out += static_cast<char>(c);
                    }
            }
        }
        return out + "\"";
    }

    EvaluateFn evaluate_;
    std::mutex ready_mutex_;
    bool ready_ = false;
    std::deque<std::string> pending_;
    std::deque<std::string> queued_;
    bool flush_in_flight_ = false;
};

} // namespace webview
} // namespace adapters

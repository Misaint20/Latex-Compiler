#pragma once

#include "core/contracts/IEventPublisher.hpp"

#include <iostream>

namespace adapters {
namespace mock {

class LogEventPublisher : public core::contracts::IEventPublisher {
public:
    void publish(const core::contracts::Event& event) override {
        std::cout << "[event] " << event.topic << ": " << event.payload << std::endl;
    }
};

} // namespace mock
} // namespace adapters

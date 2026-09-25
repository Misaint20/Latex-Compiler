#pragma once

#include <string>

namespace core {
namespace contracts {

struct Event {
    std::string topic;
    std::string payload;
};

class IEventPublisher {
public:
    virtual ~IEventPublisher() = default;
    virtual void publish(const Event& event) = 0;
};

} // namespace contracts
} // namespace core

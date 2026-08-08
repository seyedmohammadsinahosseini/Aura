#include "EventBus.h"

namespace backend {

void EventBus::subscribe(const std::string& event, Handler handler) {
    handlers_[event].push_back(std::move(handler));
}

void EventBus::publish(const std::string& event, const std::string& payload) {
    auto it = handlers_.find(event);
    if (it == handlers_.end()) {
        return;
    }
    for (auto& handler : it->second) {
        handler(event, payload);
    }
}

} // namespace backend

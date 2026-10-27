#ifndef BACKEND_CORE_EVENTBUS_H
#define BACKEND_CORE_EVENTBUS_H

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace backend {

class EventBus {
public:
    using Handler = std::function<void(const std::string& event, const std::string& payload)>;

    void subscribe(const std::string& event, Handler handler);
    void publish(const std::string& event, const std::string& payload);

private:
    std::map<std::string, std::vector<Handler>> handlers_;
};

} // namespace backend

#endif // BACKEND_CORE_EVENTBUS_H

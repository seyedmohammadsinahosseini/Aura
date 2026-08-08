#ifndef BACKEND_SERVICES_BUFFER_BUFFERSERVICE_H
#define BACKEND_SERVICES_BUFFER_BUFFERSERVICE_H

#include <cstddef>

namespace backend {
namespace services {

class BufferService {
public:
    bool initialize(std::size_t size);
    void push(const void* data, std::size_t size);
};

} // namespace services
} // namespace backend

#endif // BACKEND_SERVICES_BUFFER_BUFFERSERVICE_H

#include "BufferService.h"

namespace backend {
namespace services {

bool BufferService::initialize(std::size_t size) {
    (void)size;
    return true;
}

void BufferService::push(const void* data, std::size_t size) {
    (void)data;
    (void)size;
}

} // namespace services
} // namespace backend

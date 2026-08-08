#include "StorageService.h"

namespace backend {
namespace services {

bool StorageService::initialize(const std::string& databasePath) {
    (void)databasePath;
    return true;
}

bool StorageService::saveCrop(const std::string& imagePath) {
    (void)imagePath;
    return true;
}

} // namespace services
} // namespace backend

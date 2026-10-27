#ifndef BACKEND_SERVICES_STORAGE_STORAGESERVICE_H
#define BACKEND_SERVICES_STORAGE_STORAGESERVICE_H

#include <string>

namespace backend {
namespace services {

class StorageService {
public:
    bool initialize(const std::string& databasePath);
    bool saveCrop(const std::string& imagePath);
};

} // namespace services
} // namespace backend

#endif // BACKEND_SERVICES_STORAGE_STORAGESERVICE_H

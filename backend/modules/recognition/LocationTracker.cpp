#include "LocationTracker.h"

namespace face {

LocationTracker::LocationTracker(FaceDatabase& db) : db_(db) {
}

void LocationTracker::updateLocation(int64_t personId, int64_t zoneId, int64_t cameraId, int64_t timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    db_.logLocation(personId, zoneId, cameraId, timestamp);

    LocationRecord record;
    record.personId = personId;
    record.zoneId = zoneId;
    record.cameraId = cameraId;
    record.timestamp = timestamp;
    locations_[personId] = record;
}

std::optional<LocationRecord> LocationTracker::getLocation(int64_t personId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = locations_.find(personId);
    if (it == locations_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void LocationTracker::clearLocation(int64_t personId) {
    std::lock_guard<std::mutex> lock(mutex_);
    locations_.erase(personId);
}

std::map<int64_t, LocationRecord> LocationTracker::getAllLocations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return locations_;
}

}  // namespace face

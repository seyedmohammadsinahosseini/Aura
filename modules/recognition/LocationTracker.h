#ifndef FACE_CPP_LOCATIONTRACKER_H
#define FACE_CPP_LOCATIONTRACKER_H

#include "services/storage/FaceDatabase.h"
#include "RecognitionTypes.h"

#include <map>
#include <mutex>
#include <optional>

namespace face {

class LocationTracker {
public:
    explicit LocationTracker(FaceDatabase& db);

    void updateLocation(int64_t personId, int64_t zoneId, int64_t cameraId, int64_t timestamp);
    std::optional<LocationRecord> getLocation(int64_t personId) const;
    void clearLocation(int64_t personId);
    std::map<int64_t, LocationRecord> getAllLocations() const;

private:
    FaceDatabase& db_;
    mutable std::mutex mutex_{};
    std::map<int64_t, LocationRecord> locations_{};
};

} // namespace face

#endif // FACE_CPP_LOCATIONTRACKER_H

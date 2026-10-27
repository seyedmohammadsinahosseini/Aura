#ifndef FACE_CPP_ATTENDANCEMANAGER_H
#define FACE_CPP_ATTENDANCEMANAGER_H

#include "core/config/Config.h"
#include "services/storage/FaceDatabase.h"
#include "WorkSessionTracker.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace face {

class AttendanceManager {
public:
    struct TrackState {
        int64_t personId{-1};
        bool arrived{false};
        int64_t arrivalTime{0};
    };

    AttendanceManager(FaceDatabase& db, WorkSessionTracker& workSessionTracker, const AttendanceConfig& defaults);

    bool markArrival(const std::string& trackId, int64_t personId, int64_t timestamp);
    bool markDeparture(const std::string& trackId, int64_t personId, int64_t timestamp);
    void removeTrack(const std::string& trackId);
    std::string getCurrentStatus(const std::string& trackId);
    std::optional<TrackState> getTrackState(const std::string& trackId);
    void updateDefaults(const AttendanceConfig& defaults);

private:
    FaceDatabase& db_;
    WorkSessionTracker& workSessionTracker_;
    AttendanceConfig defaults_{};
    std::unordered_map<std::string, TrackState> tracks_{};
    std::mutex mutex_{};

    void logTardinessIfNeeded(int64_t personId, int64_t timestamp);
};

}  // namespace face

#endif  // FACE_CPP_ATTENDANCEMANAGER_H

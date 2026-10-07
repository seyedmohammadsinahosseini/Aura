#include "AttendanceManager.h"

#include <ctime>

namespace {

std::string dateFromTimestamp(const int64_t timestamp) {
    std::time_t rawTime = static_cast<std::time_t>(timestamp);
    std::tm localTime{};
    localtime_r(&rawTime, &localTime);

    char buffer[11];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &localTime);
    return buffer;
}

int64_t buildTimestampForToday(const int64_t timestamp, const std::string& hhmm) {
    std::time_t rawTime = static_cast<std::time_t>(timestamp);
    std::tm localTime{};
    localtime_r(&rawTime, &localTime);

    if (hhmm.size() >= 5) {
        localTime.tm_hour = std::stoi(hhmm.substr(0, 2));
        localTime.tm_min = std::stoi(hhmm.substr(3, 2));
    } else {
        localTime.tm_hour = 0;
        localTime.tm_min = 0;
    }
    localTime.tm_sec = 0;
    return static_cast<int64_t>(std::mktime(&localTime));
}

} // namespace

namespace face {

AttendanceManager::AttendanceManager(FaceDatabase& db, WorkSessionTracker& workSessionTracker,
                                     const AttendanceConfig& defaults)
    : db_(db), workSessionTracker_(workSessionTracker), defaults_(defaults) {}

bool AttendanceManager::markArrival(const std::string& trackId, int64_t personId,
                                    int64_t timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& state = tracks_[trackId];
    if (state.arrived) {
        return false;
    }

    state.personId = personId;
    state.arrived = true;
    state.arrivalTime = timestamp;

    workSessionTracker_.onPersonArrived(personId, timestamp);
    logTardinessIfNeeded(personId, timestamp);
    return true;
}

bool AttendanceManager::markDeparture(const std::string& trackId, int64_t personId,
                                      int64_t timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = tracks_.find(trackId);
    if (it == tracks_.end() || !it->second.arrived) {
        return false;
    }

    if (personId != -1) {
        it->second.personId = personId;
    }
    it->second.arrived = false;
    workSessionTracker_.onPersonLeft(it->second.personId, timestamp);
    return true;
}

void AttendanceManager::removeTrack(const std::string& trackId) {
    std::lock_guard<std::mutex> lock(mutex_);
    tracks_.erase(trackId);
}

std::string AttendanceManager::getCurrentStatus(const std::string& trackId) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = tracks_.find(trackId);
    if (it == tracks_.end()) {
        return "UNKNOWN";
    }
    return it->second.arrived ? "IN" : "OUT";
}

std::optional<AttendanceManager::TrackState>
AttendanceManager::getTrackState(const std::string& trackId) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = tracks_.find(trackId);
    if (it == tracks_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void AttendanceManager::updateDefaults(const AttendanceConfig& defaults) {
    std::lock_guard<std::mutex> lock(mutex_);
    defaults_ = defaults;
}

void AttendanceManager::logTardinessIfNeeded(int64_t personId, int64_t timestamp) {
    const std::string date = dateFromTimestamp(timestamp);
    const auto summary = workSessionTracker_.getDailySummary(personId, date);
    if (summary.arrivalTime != 0 && summary.arrivalTime != timestamp) {
        return;
    }

    PersonSchedule schedule;
    if (const auto stored = db_.getPersonSchedule(personId); stored.has_value()) {
        schedule = *stored;
    } else {
        schedule.personId = personId;
        schedule.expectedArrival = defaults_.defaultArrival;
        schedule.expectedDeparture = defaults_.defaultDeparture;
        schedule.graceMinutes = defaults_.lateGraceMinutes;
        schedule.workDays = "1,2,3,4,5";
    }

    const int64_t expectedArrival = buildTimestampForToday(timestamp, schedule.expectedArrival);
    const int64_t graceSeconds = static_cast<int64_t>(schedule.graceMinutes) * 60;
    if (timestamp <= expectedArrival + graceSeconds) {
        return;
    }

    TardinessRecord record;
    record.personId = personId;
    record.date = date;
    record.expectedArrival = expectedArrival;
    record.actualArrival = timestamp;
    record.tardinessSeconds = timestamp - expectedArrival;
    record.isAbsent = false;
    db_.logTardiness(record);
}

} // namespace face

#include "WorkSessionTracker.h"

#include <algorithm>
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

}  // namespace

namespace face {

WorkSessionTracker::WorkSessionTracker(FaceDatabase& db) : db_(db) {
}

void WorkSessionTracker::onPersonArrived(int64_t personId, int64_t timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string date = dateFromTimestamp(timestamp);
    auto openSession = db_.getOpenSession(personId, date);
    if (!openSession.has_value()) {
        db_.openWorkSession(personId, date, timestamp);
        return;
    }

    const auto openBreak = db_.getOpenBreak(openSession->id);
    if (openBreak.has_value()) {
        const int64_t breakSeconds = std::max<int64_t>(0, timestamp - openBreak->breakStart);
        db_.closeBreak(openBreak->id, timestamp, breakSeconds);
    }
}

void WorkSessionTracker::onPersonLeft(int64_t personId, int64_t timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string date = dateFromTimestamp(timestamp);
    const auto openSession = db_.getOpenSession(personId, date);
    if (!openSession.has_value()) {
        return;
    }

    if (db_.getOpenBreak(openSession->id).has_value()) {
        return;
    }

    db_.addBreak(openSession->id, personId, timestamp);
}

void WorkSessionTracker::onDayEnd(const std::string& /*date*/) {
    // TODO: close dangling sessions when a scheduler/API layer is introduced.
}

DailySummary WorkSessionTracker::getDailySummary(int64_t personId, const std::string& date) {
    std::lock_guard<std::mutex> lock(mutex_);
    return db_.getDailySummary(personId, date);
}

std::vector<BreakRecord> WorkSessionTracker::getTimeline(int64_t personId, const std::string& date) {
    std::lock_guard<std::mutex> lock(mutex_);
    return db_.getTimeline(personId, date);
}

}  // namespace face

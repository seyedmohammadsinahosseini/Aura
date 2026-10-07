#ifndef FACE_CPP_WORKSESSIONTRACKER_H
#define FACE_CPP_WORKSESSIONTRACKER_H

#include "services/storage/FaceDatabase.h"
#include "modules/recognition/RecognitionTypes.h"

#include <mutex>
#include <string>
#include <vector>

namespace face {

class WorkSessionTracker {
public:
    explicit WorkSessionTracker(FaceDatabase& db);

    void onPersonArrived(int64_t personId, int64_t timestamp);
    void onPersonLeft(int64_t personId, int64_t timestamp);
    void onDayEnd(const std::string& date);
    DailySummary getDailySummary(int64_t personId, const std::string& date);
    std::vector<BreakRecord> getTimeline(int64_t personId, const std::string& date);

private:
    FaceDatabase& db_;
    std::mutex mutex_{};
};

} // namespace face

#endif // FACE_CPP_WORKSESSIONTRACKER_H

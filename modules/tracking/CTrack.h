//
// Created by hashemi on 12/23/23.
//

#ifndef FACE_CPP_CTRACK_H
#define FACE_CPP_CTRACK_H

#include <vector>
#include <ByteTrack/BYTETracker.h>
#include "core/SFace.h"
#include "core/config/Config.h"

namespace face {
class CTracker {
private:
    byte_track::BYTETracker tracker_;

public:
    explicit CTracker(const FaceConfig& config);

    static std::vector<byte_track::Object<std::any>>
    ChangeToTrackerStructure(const std::vector<SFace>& detections);

    std::vector<byte_track::BYTETracker::STrackPtr> Track(std::vector<SFace>& detections);

    ~CTracker();
};
} // namespace face

#endif //FACE_CPP_CTRACK_H

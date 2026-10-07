//
// Created by hashemi on 12/23/23.
//

#include "CTrack.h"

face::CTracker::CTracker(const FaceConfig& config) {
    int frameRate = 30;
    int trackBuffer = config.TrackerBuffer();
    float trackThreshold = config.TrackerThreshold();
    float highThreshold = config.TrackerHighThreshold();
    float matchThreshold = config.TrackerMatchThreshold();

    tracker_ = byte_track::BYTETracker(frameRate, trackBuffer, trackThreshold, highThreshold,
                                       matchThreshold);
}

std::vector<byte_track::Object<std::any>>
face::CTracker::ChangeToTrackerStructure(const std::vector<SFace>& detections) {
    std::vector<byte_track::Object<std::any>> objects;
    for (int i = 0; i < detections.size(); i++) {
        //    for (const auto &detection: detections)
        byte_track::Rect<float> rect(detections[i].mBbox.x, detections[i].mBbox.y,
                                     detections[i].mBbox.width, detections[i].mBbox.height);
        byte_track::Object<std::any> obj(rect, -1, detections[i].mConfidence, i); //uniqueID
        objects.push_back(obj);
    }
    return objects;
}

std::vector<byte_track::BYTETracker::STrackPtr>
face::CTracker::Track(std::vector<SFace>& detections) {
    std::vector<byte_track::Object<std::any>> tracklets = ChangeToTrackerStructure(detections);
    auto [onlineTargets, removedTargets] = tracker_.update(tracklets);
    for (const auto& target : onlineTargets) {
        auto index = std::any_cast<int>(target->getData());
        auto trackID = target->getTrackId();
        detections[index].mTrackID = trackID;
    }
    return removedTargets;
}

face::CTracker::~CTracker() {}

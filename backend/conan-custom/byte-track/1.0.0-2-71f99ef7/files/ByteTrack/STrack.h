#pragma once

#include "Rect.h"
#include "KalmanFilter.h"
#include <any>
#include <cstddef>

namespace byte_track
{
enum class STrackState {
    New = 0,
    Tracked = 1,
    Lost = 2,
    Removed = 3,
};

class STrack
{
public:
    STrack(const Rect<float>& rect, const float& score, std::any data);
    ~STrack();

    const Rect<float>& getRect() const;
    const STrackState& getSTrackState() const;

    const bool& isActivated() const;
    const float& getScore() const;
    const std::string& getTrackId() const;
    const size_t& getFrameId() const;
    const size_t& getStartFrameId() const;
    const size_t& getTrackletLength() const;

    void activate(const size_t& frame_id, const std::string& track_id);
    void reActivate(const STrack &new_track, const size_t &frame_id);

    void predict();
    void update(const STrack &new_track, const size_t &frame_id);

    void markAsLost();
    void markAsRemoved();

    std::any getData() const;

private:
    KalmanFilter kalman_filter_;
    KalmanFilter::StateMean mean_;
    KalmanFilter::StateCov covariance_;

    Rect<float> rect_;
    STrackState state_;

    bool is_activated_;
    float score_;
    std::string track_id_;
    size_t frame_id_;
    size_t start_frame_id_;
    size_t tracklet_len_;
    std::any data_;
    void updateRect();


};
}
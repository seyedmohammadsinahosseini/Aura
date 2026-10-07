//
// Created by hashemi on 1/3/24.
//

#ifndef FACE_CPP_SFACE_H
#define FACE_CPP_SFACE_H

#include <opencv2/opencv.hpp>

namespace face {
constexpr uint32_t DIM = 512;
using FeatureArray = std::array<float, DIM>;

struct SFace {
    std::string mTrackID{};
    int64_t mPersonId{-1};
    std::string mDisplayName{};
    float mRecognitionConfidence{0.0F};

    float mConfidence{};
    cv::Rect mBbox{};
    bool face = true;
    std::vector<cv::Point> mKeypoint{};
    FeatureArray mEmbedding{};
    bool mHasEmbedding{false};

    cv::Mat mFaceImage{};

    bool mSideProfileView{};
    bool mFarDistance{};
    bool mBlurry{};
    float mBlurScore{0.0F};
};
} // namespace face
#endif //FACE_CPP_SFACE_H

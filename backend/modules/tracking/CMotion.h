#ifndef FACE_CPP_CMOTION_H
#define FACE_CPP_CMOTION_H

#include <opencv2/opencv.hpp>
#include "core/config/Config.h"

namespace face {
    class CMotion {
    private:
        cv::Mat Convert2Gray(const cv::Mat &rgbImage) const;

        bool DetectMotion(const cv::Mat &firstFrame, const cv::Mat &secondFrame);

        int mMotionThreshold;
        int mMotionBuffer;
        int mMotionDiffThreshold;
        int mMotionBlurKernelSize;
        int mMotionDilateKernelSize;
        int mHoldCount = 0;
        bool motionFlag = true;
        cv::Mat mFirstFrame;
        cv::Mat mSecondFrame;

    public:
        explicit CMotion(const FaceConfig &config);
        void updateConfig(int motionThreshold, int motionBuffer, int motionDiffThreshold, int blurKernel, int dilateKernel);

        bool PrepareMotionResult(const cv::Mat &frame);

        ~CMotion();
    };
}
#endif //FACE_CPP_CMOTION_H

#include "CMotion.h"

face::CMotion::CMotion(const FaceConfig& config)
    : mMotionThreshold(config.MotionAreaThreshold()),
      mMotionBuffer(config.MotionBufferFrames()),
      mMotionDiffThreshold(config.MotionDiffThreshold()),
      mMotionBlurKernelSize(config.MotionBlurKernelSize()),
      mMotionDilateKernelSize(config.MotionDilateKernelSize()) {
}

void face::CMotion::updateConfig(int motionThreshold, int motionBuffer, int motionDiffThreshold, int blurKernel, int dilateKernel) {
    mMotionThreshold = motionThreshold;
    mMotionBuffer = motionBuffer;
    mMotionDiffThreshold = motionDiffThreshold;
    mMotionBlurKernelSize = blurKernel;
    mMotionDilateKernelSize = dilateKernel;
}

cv::Mat face::CMotion::Convert2Gray(const cv::Mat& rgbImage) const {
    cv::Mat grayImage;
    cv::cvtColor(rgbImage, grayImage, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(grayImage,
                     grayImage,
                     cv::Size(mMotionBlurKernelSize, mMotionBlurKernelSize),
                     0);
    return grayImage;
}

bool face::CMotion::DetectMotion(const cv::Mat& firstFrame, const cv::Mat& secondFrame) {
    bool result = false;
    cv::Mat frameDelta;
    cv::absdiff(firstFrame, secondFrame, frameDelta);

    cv::Mat kernel = cv::Mat::ones(mMotionDilateKernelSize, mMotionDilateKernelSize, CV_8U);
    cv::dilate(frameDelta, frameDelta, kernel, cv::Point(-1, -1), 1, cv::BORDER_CONSTANT, cv::morphologyDefaultBorderValue());

    cv::Mat thresh;
    cv::threshold(frameDelta, thresh, mMotionDiffThreshold, 255, cv::THRESH_BINARY);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(thresh, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    for (const auto& contour : contours) {
        if (cv::contourArea(contour) > mMotionThreshold) {
            result = true;
            break;
        }
    }
    return result;
}

bool face::CMotion::PrepareMotionResult(const cv::Mat& frame) {
    mSecondFrame = Convert2Gray(frame);

    if (!mFirstFrame.empty() && !mSecondFrame.empty()) {
        motionFlag = DetectMotion(mFirstFrame, mSecondFrame);

        if (motionFlag) {
            // Release mHoldCount if motion is detected
            mHoldCount = 0;
        } else {
            // Keep motion true for mMotionBuffer frames after detecting motion
            if (mHoldCount < mMotionBuffer) {
                motionFlag = true;
            }
        }
    }
    mHoldCount++;
    mFirstFrame = mSecondFrame;
    return motionFlag;
}

face::CMotion::~CMotion() {
}


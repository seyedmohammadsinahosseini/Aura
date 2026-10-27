#include "CFar.h"

face::CFar::CFar() {
}

bool face::CFar::CheckSize(const std::pair<int, int> &frameInfo, const cv::Rect &bbox) const {
    bool mFlag = false;
    if (((static_cast<double>(bbox.height) / frameInfo.first) < mFarHeight) &&
        ((static_cast<double>(bbox.width) / frameInfo.second) < mFarWidth)) {
        mFlag = true;
    }
    return mFlag;
}

face::CFar::~CFar() {
}

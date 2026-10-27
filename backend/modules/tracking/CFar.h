#ifndef FACE_CPP_CFAR_H
#define FACE_CPP_CFAR_H

#include <opencv2/opencv.hpp>

namespace face {
    class CFar {


    public:
        explicit CFar();
        float mFarHeight;
        float mFarWidth;
        bool CheckSize(const std::pair<int, int> &frameInfo, const cv::Rect &bbox) const;

        ~CFar();
    };
}

#endif //FACE_CPP_CFAR_H

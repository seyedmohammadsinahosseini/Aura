#ifndef FACE_CPP_CPROFILE_H
#define FACE_CPP_CPROFILE_H

#include <opencv2/opencv.hpp>

namespace face {
    class CProfile {
    private:

        static double CalculateDistance(const cv::Point &x, const cv::Point &y);

        static std::tuple<double, double, double, std::string>
        CalculateAngle(const cv::Point &A, const cv::Point &B, const cv::Point &C);

        static double CalculateArea(const cv::Point &X, const cv::Point &Y, const cv::Point &Z);


    public:
        explicit CProfile();

        bool CheckSideProfile(const std::vector<cv::Point> &keypoint);

        ~CProfile();
    };
}

#endif //FACE_CPP_CPROFILE_H

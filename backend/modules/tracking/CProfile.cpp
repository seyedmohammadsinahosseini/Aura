#include "CProfile.h"

face::CProfile::CProfile()  {

}

double face::CProfile::CalculateDistance(const cv::Point& X, const cv::Point& Y) {
    double xDiff = X.x - Y.x;
    double yDiff = X.y - Y.y;
    double distance = xDiff * xDiff + yDiff * yDiff;
    return distance;
}

std::tuple<double, double, double, std::string>
face::CProfile::CalculateAngle(const cv::Point &A, const cv::Point &B, const cv::Point &C) {
    double a2 = CalculateDistance(B, C);
    double b2 = CalculateDistance(A, C);
    double c2 = CalculateDistance(A, B);
    std::string error;

    // Handle cases where a, b, or c is zero to avoid division by zero
    if (a2 == 0 || b2 == 0 || c2 == 0) {
        error = "Zero Side";
        return std::make_tuple(0.0, 0.0, 0.0, error);
    }

    // Calculate angles using Cosine law
    double a = std::sqrt(a2);
    double b = std::sqrt(b2);
    double c = std::sqrt(c2);
    double alpha = std::acos((b2 + c2 - a2) / (2 * b * c));
    double beta = std::acos((a2 + c2 - b2) / (2 * a * c));
    double gamma = std::acos((a2 + b2 - c2) / (2 * a * b));

    // Converting to degree
    alpha = alpha * 180.0 / CV_PI;
    beta = beta * 180.0 / CV_PI;
    gamma = gamma * 180.0 / CV_PI;

    return std::make_tuple(alpha, beta, gamma, error);
}

double face::CProfile::CalculateArea(const cv::Point &X, const cv::Point &Y, const cv::Point &Z) {
    double area = std::abs(0.5 * (X.x * (Y.y - Z.y) + Y.x * (Z.y - X.y) + Z.x * (X.y - Y.y)));
    return area;
}

bool face::CProfile::CheckSideProfile(const std::vector<cv::Point> &keypoint) {
    bool mFlag = false;

    // Calculate angles of bottom triangle (nose key point and mouth key point)
    auto [a_b_nose, a_l_lip, a_r_lip, errorMouth] = CalculateAngle(keypoint[2], keypoint[3], keypoint[4]);

    // Calculate angles of top triangle (nose key point and eyes key point)
    auto [a_t_nose, a_l_eye, a_r_eye, errorEye] = CalculateAngle(keypoint[2], keypoint[0], keypoint[1]);

    // Calculate area of bottom triangle
    double triangleAreaBottom = CalculateArea(keypoint[2], keypoint[3], keypoint[4]);

    // Calculate area of top triangle
    double triangleAreaTop = CalculateArea(keypoint[2], keypoint[0], keypoint[1]);

    // Calculate ratio of areas to find downside face
    std::string errorArea;
    double areaRatio;
    if (triangleAreaBottom == 0 || triangleAreaTop == 0) {
        errorArea = "Zero Area";
    } else {
        areaRatio = triangleAreaBottom / triangleAreaTop;
    }

    // Check if face is profile or not
    if (!errorMouth.empty() || !errorEye.empty() ||
        a_l_lip > 90.0 || a_r_lip > 90.0 || a_l_eye > 90.0 || a_r_eye > 90.0 ||
        !errorArea.empty() ) {
        mFlag = true;
    }
    return mFlag;
}

face::CProfile::~CProfile() {
}

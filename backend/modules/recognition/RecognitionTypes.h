#ifndef FACE_CPP_RECOGNITIONTYPES_H
#define FACE_CPP_RECOGNITIONTYPES_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace face {

enum class CameraType { ENTRY, EXIT, BOTH };
enum class Direction { IN, OUT, UNKNOWN };

struct CameraConfig {
    int64_t id{0};
    std::string uid{};
    std::string name{};
    int64_t zoneId{0};
    CameraType type{CameraType::BOTH};
    bool streamEnabled{true};
    bool aiEnabled{true};
    int preferredFps{0};
    int preferredWidth{0};
    int preferredHeight{0};
    std::string connectionType{"RTSP"};
    std::string username{};
    std::string password{};
    // ONVIF metadata persisted from discovery
    std::string streamUri{};
    std::string encoding{};
    std::string onvifXaddrs{};
    std::string transportProtocol{"tcp"};
};

struct ZoneInfo {
    int64_t id{0};
    std::string name{};
    std::string description{};
    int64_t createdAt{0};
    int requiredAccessLevel{2};
};

struct PersonSchedule {
    int64_t personId{0};
    std::string expectedArrival{"08:00"};
    std::string expectedDeparture{"17:00"};
    int graceMinutes{15};
    std::string workDays{"1,2,3,4,5"};
};

struct WorkSession {
    int64_t id{0};
    int64_t personId{0};
    std::string date{};
    int64_t arrivalTime{0};
    int64_t departureTime{0};
    int64_t totalWorkSeconds{0};
    int64_t totalBreakSeconds{0};
    bool isComplete{false};
};

struct BreakRecord {
    int64_t id{0};
    int64_t sessionId{0};
    int64_t personId{0};
    int64_t breakStart{0};
    int64_t breakEnd{0};
    int64_t breakSeconds{0};
};

struct LocationRecord {
    int64_t personId{0};
    int64_t zoneId{0};
    int64_t cameraId{0};
    int64_t timestamp{0};
    std::string zoneName{};
};

struct TardinessRecord {
    int64_t personId{0};
    std::string date{};
    int64_t expectedArrival{0};
    int64_t actualArrival{0};
    int64_t tardinessSeconds{0};
    bool isAbsent{false};
};

struct DailySummary {
    int64_t personId{0};
    std::string date{};
    int64_t arrivalTime{0};
    int64_t departureTime{0};
    int64_t workSeconds{0};
    int64_t breakSeconds{0};
    int64_t tardinessSeconds{0};
    bool isAbsent{false};
    std::vector<std::pair<int64_t, int64_t>> breakIntervals{};
};

struct MotionConfig {
    int windowSize{10};
    float threshold{15.0F};
    bool useVertical{false};
};

}  // namespace face

#endif  // FACE_CPP_RECOGNITIONTYPES_H

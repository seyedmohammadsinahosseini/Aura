#ifndef FACE_CPP_FACEDATABASE_H
#define FACE_CPP_FACEDATABASE_H

#include "modules/recognition/RecognitionTypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <opencv2/opencv.hpp>

struct sqlite3;

namespace face {

struct Person {
    int64_t id{0};
    std::string name{};
    std::string code{};
    std::string picturePath{};
    int64_t createdAt{0};
    int access_level{2};
};

struct RawDetection {
    int64_t id{0};
    int64_t personId{-1};
    std::string trackId{};
    std::vector<float> embedding{};
    std::string imagePath{};
    int64_t timestamp{0};
    std::string status{};
};

struct UnknownFace {
    int64_t id{0};
    std::vector<float> embedding{};
    std::string trackId{};
    std::string cameraUid{};
    int64_t timestamp{0};
    std::string imagePath{};
};

class FaceDatabase {
public:
    explicit FaceDatabase(const std::string& dbPath);
    ~FaceDatabase();

    // Person management (bulk)
    std::vector<Person> getAllPersons() const;
    bool updatePerson(int64_t personId, const std::string& name, const std::string& code);
    bool deletePerson(int64_t personId);
    bool updatePersonAccessLevel(int64_t personId, int accessLevel);
    bool updatePersonPicturePath(int64_t personId, const std::string& picturePath);

    int64_t addPerson(const std::string& name, const std::string& code, bool isKnown, const std::string& picturePath = "");
    int64_t addPerson(const std::string& name, const std::string& code, bool isKnown, const cv::Mat& image, const std::string& cropsDir);
    bool getPerson(int64_t id, Person& out);
    void addReferenceEmbedding(int64_t personId, const std::vector<float>& emb);
    void addUnknownFace(const std::vector<float>& embedding, const std::string& trackId, const std::string& cameraUid, int64_t timestamp, const std::string& imagePath = "");
    bool unknownFaceExists(const std::string& trackId, const std::string& cameraUid);
    std::vector<std::pair<int64_t, std::vector<float>>> getAllReferenceEmbeddings();
    int64_t addRawDetection(const RawDetection& detection);
    void addAttendanceLog(int64_t personId,
                          const std::string& trackId,
                          const std::string& eventType,
                          int64_t timestamp);
    std::vector<RawDetection> getUnknownDetectionsNotProcessed();
    void updateRawDetectionPersonId(int64_t detectionId, int64_t personId);

    // Unknown face review operations
    std::vector<UnknownFace> getUnknownFaces() const;
    std::optional<std::string> getUnknownFaceImagePath(int64_t id);
    bool deleteUnknownFace(int64_t id);
    bool deleteUnknownFaceByTrackId(const std::string& trackId, const std::string& cameraUid);
    bool updateUnknownFaceEmbedding(const std::string& trackId,
                                    const std::string& cameraUid,
                                    const std::vector<float>& newEmbedding);
    int64_t enrollUnknownAsNewPerson(int64_t unknownFaceId, const std::string& name, const std::string& code, int accessLevel);
    bool mergeUnknownWithPerson(int64_t unknownFaceId, int64_t personId);

    int64_t upsertCamera(const CameraConfig& camera);
    std::optional<CameraConfig> getCameraConfig(const std::string& cameraUid);
    std::vector<CameraConfig> getAllCameras();
    bool deleteCamera(const std::string& cameraUid);
    bool updateCameraStreamEnabled(const std::string& cameraUid, bool enabled);
    bool updateCameraAiEnabled(const std::string& cameraUid, bool enabled);

    int64_t addZone(const ZoneInfo& zone);
    std::optional<ZoneInfo> getZone(int64_t zoneId);
    std::vector<ZoneInfo> getAllZones();
    bool updateZoneRequiredAccessLevel(int64_t zoneId, int requiredAccessLevel);

    int64_t openWorkSession(int64_t personId, const std::string& date, int64_t timestamp);
    void closeWorkSession(int64_t sessionId, int64_t timestamp, int64_t workSeconds, int64_t breakSeconds);
    std::optional<WorkSession> getOpenSession(int64_t personId, const std::string& date);

    int64_t addBreak(int64_t sessionId, int64_t personId, int64_t timestamp);
    void closeBreak(int64_t breakId, int64_t timestamp, int64_t seconds);
    std::optional<BreakRecord> getOpenBreak(int64_t sessionId);
    std::vector<BreakRecord> getTimeline(int64_t personId, const std::string& date);

    void logLocation(int64_t personId, int64_t zoneId, int64_t cameraId, int64_t timestamp);
    std::optional<LocationRecord> getPersonCurrentLocation(int64_t personId);

    void logTardiness(const TardinessRecord& record);
    std::vector<TardinessRecord> getTardinessList(const std::string& date);
    DailySummary getDailySummary(int64_t personId, const std::string& date);

    void upsertPersonSchedule(const PersonSchedule& schedule);
    std::optional<PersonSchedule> getPersonSchedule(int64_t personId);

    void setSetting(const std::string& key, const std::string& value, const std::string& description = "");
    std::optional<std::string> getSetting(const std::string& key);

    // Module configuration per camera
    std::optional<std::pair<bool, std::string>> getCameraModuleConfig(const std::string& cameraUid, const std::string& moduleName);
    void setCameraModuleConfig(const std::string& cameraUid, const std::string& moduleName, bool enabled, const std::string& configJson);

private:
    sqlite3* db_{nullptr};

    void open(const std::string& dbPath);
    void createTables();
    void execute(const std::string& sql);
    bool hasColumn(const std::string& tableName, const std::string& columnName);
    static void validateEmbeddingSize(const std::vector<float>& emb);
};

}  // namespace face

#endif  // FACE_CPP_FACEDATABASE_H

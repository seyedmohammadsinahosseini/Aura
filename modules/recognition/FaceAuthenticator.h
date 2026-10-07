#ifndef FACE_CPP_FACEAUTHENTICATOR_H
#define FACE_CPP_FACEAUTHENTICATOR_H

#include "modules/pipeline/FacePipeline.h"
#include "core/SFace.h"
#include "core/PluginRegistry.h"
#include "core/config/Config.h"
#include "core/license/LicenseManager.h"
#include "modules/draw/DetectionDrawer.h"
#include "modules/draw/RecognitionDrawer.h"
#include "modules/attendance/AttendanceManager.h"
#include "services/storage/FaceDatabase.h"
#include "FaceSearcher.h"
#include "LocationTracker.h"
#include "MotionAnalyzer.h"
#include "RecognitionTypes.h"
#include "modules/attendance/WorkSessionTracker.h"

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <opencv2/opencv.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace face {

class FaceAuthenticator {
public:
    explicit FaceAuthenticator(const std::shared_ptr<FaceConfig>& config);

    std::vector<SFace> processFrame(const std::string& cameraUid, cv::Mat& frame);
    void drawFrame(const std::string& cameraUid, cv::Mat& frame,
                   const std::vector<SFace>& detections);
    void reloadIndex();
    void reloadRuntimeSettings();

    int64_t enrollPerson(const std::string& name, const std::string& code,
                         const std::vector<float>& emb);
    int64_t addPerson(const std::string& name, const std::string& code, const cv::Mat& image,
                      std::vector<float>& emb);
    void addReferenceEmbedding(int64_t personId, const std::vector<float>& emb);
    int64_t identify(const std::vector<float>& emb, float& confidence);

    void processDetection(face::SFace& face, const std::string& trackId,
                          const std::string& cameraUid, int64_t timestamp,
                          const cv::Mat& originalFrame);
    void onTrackLost(const std::string& trackId);
    void ResetUnknownCropCache();

    int64_t upsertCamera(const CameraConfig& camera);
    std::optional<CameraConfig> getCameraConfig(const std::string& cameraUid);
    std::vector<CameraConfig> getAllCameras();
    std::optional<std::pair<bool, std::string>>
    getCameraModuleConfig(const std::string& cameraUid, const std::string& moduleName);
    void setCameraModuleConfig(const std::string& cameraUid, const std::string& moduleName,
                               bool enabled, const std::string& configJson);

    void upsertSchedule(const PersonSchedule& schedule);
    std::optional<PersonSchedule> getPersonSchedule(int64_t personId);

    std::optional<LocationRecord> getPersonLocation(int64_t personId);
    std::map<int64_t, LocationRecord> getAllCurrentLocations();
    DailySummary getDailySummary(int64_t personId, const std::string& date);
    std::vector<BreakRecord> getTimeline(int64_t personId, const std::string& date);
    std::vector<TardinessRecord> getTardinessList(const std::string& date);

private:
    struct CameraRuntime {
        std::unique_ptr<FacePipeline> pipeline{};
        backend::PluginRegistry registry{};
    };

    std::shared_ptr<FaceConfig> config_{};
    std::unique_ptr<FaceDatabase> db_{};
    std::unique_ptr<FaceSearcher> searcher_{};
    std::unique_ptr<WorkSessionTracker> workSessionTracker_{};
    std::unique_ptr<AttendanceManager> attendanceMgr_{};
    std::unique_ptr<MotionAnalyzer> motionAnalyzer_{};
    std::unique_ptr<LocationTracker> locationTracker_{};
    backend::LicenseManager licenseManager_{};

    RecognitionConfig recognitionConfig_{};
    AttendanceConfig attendanceConfig_{};
    float threshold_{0.4F};

    std::map<std::string, CameraRuntime> runtimes_{};
    std::mutex mutex_{};

    CameraRuntime& getOrCreateRuntime(const std::string& cameraUid);
    std::optional<CameraConfig> getOrCreateDefaultCamera(const std::string& cameraUid);
    void rebuildRuntimeModules(const std::string& cameraUid, CameraRuntime& runtime);
    void applyRuntimeModules(const std::string& cameraUid, CameraRuntime& runtime);
    static void registerBuiltInModules(backend::PluginRegistry& registry);
    static backend::ModuleConfig toBackendConfig(const face::ModuleConfig& config);
    static backend::ModuleConfig
    mergeModuleConfig(const backend::ModuleConfig& baseConfig,
                      const std::optional<std::pair<bool, std::string>>& overrideConfig);
    static bool isModuleRunning(const backend::PluginRegistry& registry,
                                const std::string& moduleName);
    static bool boolValue(const std::unordered_map<std::string, std::string>& params,
                          const std::string& key, bool fallback);
    static int intValue(const std::unordered_map<std::string, std::string>& params,
                        const std::string& key, int fallback);
    static float floatValue(const std::unordered_map<std::string, std::string>& params,
                            const std::string& key, float fallback);
    static std::string lookupPersonName(FaceDatabase& db, int64_t personId);
    static std::vector<float> toVector(const FeatureArray& embedding);
    static int64_t currentUnixTimestamp();
    static std::string buildScopedTrackId(const std::string& cameraUid, const std::string& trackId);
    static std::string extractCameraUid(const std::string& scopedTrackId);

    struct UnknownTrackState {
        int64_t unknownPersonId = 0;
        bool arrived = false;
        int64_t arrivalTime = 0;
    };
    std::unordered_map<std::string, UnknownTrackState> unknownTracks_;
    std::mutex unknownMutex_;
    std::unordered_set<std::string> unknown_cropped_track_ids_;
};

} // namespace face

#endif // FACE_CPP_FACEAUTHENTICATOR_H

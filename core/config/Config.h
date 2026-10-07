#ifndef FACE_CPP_FACECONFIG_H
#define FACE_CPP_FACECONFIG_H

#include <base_component/monitoring/log/log_utils.h>
#include <unordered_map>
#include <string>

namespace face {

struct ModuleConfig {
    bool enabled{true};
    std::unordered_map<std::string, std::string> parameters{};
    std::string rawJson{"{}"};
};

struct DatabaseConfig {
    std::string path{"attendance.db"};
    std::string cropsDir{"crops"};
};

struct RecognitionConfig {
    float faissThreshold{0.35F};
    int motionWindowSize{10};
    float motionThresholdPx{15.0F};
};

struct AttendanceConfig {
    std::string defaultArrival{"08:00"};
    std::string defaultDeparture{"17:00"};
    int lateGraceMinutes{15};
};

class FaceConfig {
public:
    explicit FaceConfig(const std::string& configPath = "core/config/config.json");

    bool IsValid() const;

    const std::string& DetectionModelOnnx() const;
    const std::string& DetectionModelEngine() const;
    float FaceDetectionThreshold() const;

    const std::string& RecognitionModelOnnx() const;
    const std::string& RecognitionModelEngine() const;

    const std::string& HeadDetectionModelOnnx() const;
    const std::string& HeadDetectionModelEngine() const;
    float HeadDetectionThreshold() const;

    int TrackerBuffer() const;
    float TrackerThreshold() const;
    float TrackerHighThreshold() const;
    float TrackerMatchThreshold() const;

    float FarHeightThreshold() const;
    float FarWidthThreshold() const;
    int MotionAreaThreshold() const;
    int MotionBufferFrames() const;
    int MotionDiffThreshold() const;
    int MotionBlurKernelSize() const;
    int MotionDilateKernelSize() const;
    float HeadFaceIouThreshold() const;
    int EmbeddingQueueMaxSize() const;
    bool GetQualityGateEnabled() const;
    bool GetUseBlurGate() const;
    float GetBlurThreshold() const;
    bool GetUseSideProfileGate() const;
    bool GetUseFarDistanceGate() const;
    const DatabaseConfig& Database() const;
    const RecognitionConfig& Recognition() const;
    const AttendanceConfig& Attendance() const;
    const std::unordered_map<std::string, ModuleConfig>& Modules() const;
    const std::string& LicensePath() const;

    const std::string& LogProvider() const;
    const xbox::base_component::monitoring::log::LogCallback& LogCallback() const;

    bool Reload();

private:
    bool valid_{false};
    std::string config_path_{};
    std::string license_path_{"core/config/license.json"};

    std::string detection_model_onnx_{"core/models/det_500m_FP32.onnx"};
    std::string detection_model_engine_{"core/models/det_500m_FP32.engine"};
    float detection_threshold_{0.4F};

    std::string recognition_model_onnx_{"core/models/webface_r50.onnx"};
    std::string recognition_model_engine_{"core/models/webface_r50.engine"};

    std::string head_model_onnx_{"core/models/headdetection_v12.onnx"};
    std::string head_model_engine_{"core/models/headdetection_v12.engine"};
    float head_threshold_{0.3F};

    int track_buffer_{30};
    float track_threshold_{0.5F};
    float track_high_threshold_{0.6F};
    float track_match_threshold_{0.8F};

    float far_height_threshold_{0.12F};
    float far_width_threshold_{0.12F};
    int motion_area_threshold_{1000};
    int motion_buffer_frames_{5};
    int motion_diff_threshold_{20};
    int motion_blur_kernel_size_{5};
    int motion_dilate_kernel_size_{5};
    float head_face_iou_threshold_{0.2F};
    int embedding_queue_max_size_{128};
    DatabaseConfig database_{};
    RecognitionConfig recognition_{};
    AttendanceConfig attendance_{};
    bool quality_gate_enabled_{false};
    bool use_blur_gate_{true};
    float blur_threshold_{80.0F};
    bool use_side_profile_gate_{true};
    bool use_far_distance_gate_{true};
    std::unordered_map<std::string, ModuleConfig> modules_{};

    std::string log_provider_{"face"};
    xbox::base_component::monitoring::log::LogCallback log_callback_{};

    void Load();
};

} // namespace face

#endif // FACE_CPP_FACECONFIG_H

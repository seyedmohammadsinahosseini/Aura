#include "Config.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <nlohmann/json.hpp>

namespace face {
namespace {

std::string NormalizePath(const std::filesystem::path& path) {
    return std::filesystem::absolute(path).lexically_normal().string();
}

std::unordered_map<std::string, std::string> flattenParameters(const nlohmann::json& jsonValue) {
    std::unordered_map<std::string, std::string> values;
    if (!jsonValue.is_object()) {
        return values;
    }

    for (auto it = jsonValue.begin(); it != jsonValue.end(); ++it) {
        if (it.key() == "enabled") {
            continue;
        }

        if (it.value().is_string()) {
            values[it.key()] = it.value().get<std::string>();
        } else if (it.value().is_boolean()) {
            values[it.key()] = it.value().get<bool>() ? "true" : "false";
        } else if (it.value().is_number_integer()) {
            values[it.key()] = std::to_string(it.value().get<long long>());
        } else if (it.value().is_number_unsigned()) {
            values[it.key()] = std::to_string(it.value().get<unsigned long long>());
        } else if (it.value().is_number_float()) {
            values[it.key()] = std::to_string(it.value().get<double>());
        } else {
            values[it.key()] = it.value().dump();
        }
    }

    return values;
}

std::string ResolveRelativePath(const std::string& value, const std::filesystem::path& configPath) {
    if (value.empty()) {
        return value;
    }

    const std::filesystem::path rawPath(value);
    if (rawPath.is_absolute()) {
        return rawPath.lexically_normal().string();
    }

    const auto configDir = std::filesystem::absolute(configPath).parent_path();
    const std::vector<std::filesystem::path> bases{
        std::filesystem::current_path(),
        configDir,
        configDir.parent_path(),
        configDir.parent_path().parent_path(),
    };

    for (const auto& base : bases) {
        const auto candidate = (base / rawPath).lexically_normal();
        if (std::filesystem::exists(candidate)) {
            return std::filesystem::weakly_canonical(candidate).string();
        }
    }

    for (const auto& base : bases) {
        const auto candidate = (base / rawPath).lexically_normal();
        if (std::filesystem::exists(candidate.parent_path())) {
            return NormalizePath(candidate);
        }
    }

    return NormalizePath(rawPath);
}

} // namespace

FaceConfig::FaceConfig(const std::string& configPath)
    : config_path_(configPath),
      log_callback_([](const xbox::base_component::monitoring::log::LogEntry&) {}) {
    Load();
}

bool FaceConfig::Reload() {
    Load();
    return valid_;
}

void FaceConfig::Load() {
    valid_ = false;
    modules_.clear();

    if (!std::filesystem::exists(config_path_)) {
        std::cerr << "Config file not found: " << config_path_ << std::endl;
        valid_ = false;
        return;
    }

    std::ifstream file(config_path_);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << config_path_ << std::endl;
        valid_ = false;
        return;
    }

    nlohmann::json config;
    try {
        file >> config;

        if (config.contains("models")) {
            const auto& models = config["models"];
            detection_model_onnx_ = models.value("detection_onnx", detection_model_onnx_);
            detection_model_engine_ = models.value("detection_engine", detection_model_engine_);
            recognition_model_onnx_ = models.value("recognition_onnx", recognition_model_onnx_);
            recognition_model_engine_ =
                models.value("recognition_engine", recognition_model_engine_);
            head_model_onnx_ = models.value("head_onnx", head_model_onnx_);
            head_model_engine_ = models.value("head_engine", head_model_engine_);
        }
        detection_model_onnx_ = ResolveRelativePath(detection_model_onnx_, config_path_);
        detection_model_engine_ = ResolveRelativePath(detection_model_engine_, config_path_);
        recognition_model_onnx_ = ResolveRelativePath(recognition_model_onnx_, config_path_);
        recognition_model_engine_ = ResolveRelativePath(recognition_model_engine_, config_path_);
        head_model_onnx_ = ResolveRelativePath(head_model_onnx_, config_path_);
        head_model_engine_ = ResolveRelativePath(head_model_engine_, config_path_);

        if (config.contains("thresholds")) {
            const auto& thresholds = config["thresholds"];
            detection_threshold_ = thresholds.value("detection", detection_threshold_);
            head_threshold_ = thresholds.value("head", head_threshold_);
            far_height_threshold_ = thresholds.value("far_height", far_height_threshold_);
            far_width_threshold_ = thresholds.value("far_width", far_width_threshold_);
            head_face_iou_threshold_ = thresholds.value("head_face_iou", head_face_iou_threshold_);
        }

        if (config.contains("tracker")) {
            const auto& tracker = config["tracker"];
            track_buffer_ = tracker.value("track_buffer", track_buffer_);
            track_threshold_ = tracker.value("track_threshold", track_threshold_);
            track_high_threshold_ = tracker.value("high_threshold", track_high_threshold_);
            track_match_threshold_ = tracker.value("match_threshold", track_match_threshold_);
        }

        if (config.contains("motion")) {
            const auto& motion = config["motion"];
            motion_area_threshold_ = motion.value("area_threshold", motion_area_threshold_);
            motion_buffer_frames_ = motion.value("buffer_frames", motion_buffer_frames_);
            motion_diff_threshold_ = motion.value("diff_threshold", motion_diff_threshold_);
            motion_blur_kernel_size_ = motion.value("blur_kernel_size", motion_blur_kernel_size_);
            motion_dilate_kernel_size_ =
                motion.value("dilate_kernel_size", motion_dilate_kernel_size_);
        }

        if (config.contains("pipeline")) {
            const auto& pipeline = config["pipeline"];
            embedding_queue_max_size_ =
                pipeline.value("embedding_queue_max_size", embedding_queue_max_size_);
        }

        if (config.contains("database")) {
            const auto& database = config["database"];
            database_.path = database.value("path", database_.path);
            database_.cropsDir = database.value("crops_dir", database_.cropsDir);
        }
        database_.path = ResolveRelativePath(database_.path, config_path_);
        database_.cropsDir = ResolveRelativePath(database_.cropsDir, config_path_);

        if (config.contains("recognition")) {
            const auto& recognition = config["recognition"];
            recognition_.faissThreshold =
                recognition.value("faiss_threshold", recognition_.faissThreshold);
            recognition_.motionWindowSize =
                recognition.value("motion_window_size", recognition_.motionWindowSize);
            recognition_.motionThresholdPx =
                recognition.value("motion_threshold_px", recognition_.motionThresholdPx);
        }

        if (config.contains("attendance")) {
            const auto& attendance = config["attendance"];
            attendance_.defaultArrival =
                attendance.value("default_arrival", attendance_.defaultArrival);
            attendance_.defaultDeparture =
                attendance.value("default_departure", attendance_.defaultDeparture);
            attendance_.lateGraceMinutes =
                attendance.value("late_grace_minutes", attendance_.lateGraceMinutes);
        }

        if (config.contains("license")) {
            const auto& license = config["license"];
            license_path_ = license.value("path", license_path_);
        }
        license_path_ = ResolveRelativePath(license_path_, config_path_);

        if (config.contains("modules") && config["modules"].is_object()) {
            for (auto it = config["modules"].begin(); it != config["modules"].end(); ++it) {
                ModuleConfig moduleConfig;
                if (it.value().is_object()) {
                    moduleConfig.enabled = it.value().value("enabled", moduleConfig.enabled);
                    moduleConfig.parameters = flattenParameters(it.value());
                    moduleConfig.rawJson = it.value().dump();
                }
                modules_[it.key()] = std::move(moduleConfig);
            }
            if (modules_.contains("quality_gate")) {
                const auto& qg = config["modules"]["quality_gate"];
                quality_gate_enabled_ = qg.value("enabled", quality_gate_enabled_);
                use_blur_gate_ = qg.value("use_blur_gate", use_blur_gate_);
                blur_threshold_ = qg.value("blur_threshold", blur_threshold_);
                use_side_profile_gate_ = qg.value("use_side_profile_gate", use_side_profile_gate_);
                use_far_distance_gate_ = qg.value("use_far_distance_gate", use_far_distance_gate_);
            }
        }

        valid_ = true;
    } catch (const std::exception& exception) {
        std::cerr << "Failed to parse config file: " << exception.what() << std::endl;
        valid_ = false;
    }
}

bool FaceConfig::IsValid() const {
    return valid_;
}

const std::string& FaceConfig::DetectionModelOnnx() const {
    return detection_model_onnx_;
}
const std::string& FaceConfig::DetectionModelEngine() const {
    return detection_model_engine_;
}
float FaceConfig::FaceDetectionThreshold() const {
    return detection_threshold_;
}

const std::string& FaceConfig::RecognitionModelOnnx() const {
    return recognition_model_onnx_;
}
const std::string& FaceConfig::RecognitionModelEngine() const {
    return recognition_model_engine_;
}

const std::string& FaceConfig::HeadDetectionModelOnnx() const {
    return head_model_onnx_;
}
const std::string& FaceConfig::HeadDetectionModelEngine() const {
    return head_model_engine_;
}
float FaceConfig::HeadDetectionThreshold() const {
    return head_threshold_;
}

int FaceConfig::TrackerBuffer() const {
    return track_buffer_;
}
float FaceConfig::TrackerThreshold() const {
    return track_threshold_;
}
float FaceConfig::TrackerHighThreshold() const {
    return track_high_threshold_;
}
float FaceConfig::TrackerMatchThreshold() const {
    return track_match_threshold_;
}

float FaceConfig::FarHeightThreshold() const {
    return far_height_threshold_;
}
float FaceConfig::FarWidthThreshold() const {
    return far_width_threshold_;
}
int FaceConfig::MotionAreaThreshold() const {
    return motion_area_threshold_;
}
int FaceConfig::MotionBufferFrames() const {
    return motion_buffer_frames_;
}
int FaceConfig::MotionDiffThreshold() const {
    return motion_diff_threshold_;
}
int FaceConfig::MotionBlurKernelSize() const {
    return motion_blur_kernel_size_;
}
int FaceConfig::MotionDilateKernelSize() const {
    return motion_dilate_kernel_size_;
}
float FaceConfig::HeadFaceIouThreshold() const {
    return head_face_iou_threshold_;
}
int FaceConfig::EmbeddingQueueMaxSize() const {
    return embedding_queue_max_size_;
}
const DatabaseConfig& FaceConfig::Database() const {
    return database_;
}
const RecognitionConfig& FaceConfig::Recognition() const {
    return recognition_;
}
const AttendanceConfig& FaceConfig::Attendance() const {
    return attendance_;
}
const std::unordered_map<std::string, ModuleConfig>& FaceConfig::Modules() const {
    return modules_;
}
const std::string& FaceConfig::LicensePath() const {
    return license_path_;
}

bool FaceConfig::GetQualityGateEnabled() const {
    return quality_gate_enabled_;
}
bool FaceConfig::GetUseBlurGate() const {
    return use_blur_gate_;
}
float FaceConfig::GetBlurThreshold() const {
    return blur_threshold_;
}
bool FaceConfig::GetUseSideProfileGate() const {
    return use_side_profile_gate_;
}
bool FaceConfig::GetUseFarDistanceGate() const {
    return use_far_distance_gate_;
}

const std::string& FaceConfig::LogProvider() const {
    return log_provider_;
}
const xbox::base_component::monitoring::log::LogCallback& FaceConfig::LogCallback() const {
    return log_callback_;
}

} // namespace face

# FaceAuthentication — Complete Project Analysis

> Generated: 2026-06-15
> Purpose: Comprehensive documentation for AI-assisted decision making on the FaceAuthentication project

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Backend Architecture](#2-backend-architecture)
3. [Database Schema (SQLite)](#3-database-schema)
4. [Processing Paths](#4-processing-paths)
5. [Flutter Frontend](#5-flutter-frontend)
6. [Current Face Processing Flow](#6-current-face-processing-flow)
7. [Identified Problems](#7-identified-problems)
8. [Existing Code Not Being Used](#8-existing-code-not-being-used)
9. [The AGENT_BRIEF Document](#9-the-agent_brief-document)
10. [Configuration System](#10-configuration-system)
11. [Build System](#11-build-system)
12. [Key Files Reference](#12-key-files-reference)

---

## 1. Project Overview

**FaceAuthentication** is a real-time face recognition and attendance tracking system. It consists of:

- **Backend**: C++20 application built with CMake + Conan, producing:
  - `libface_backend.so` — Shared library consumed by Flutter via FFI
  - `FaceAuthentication` — Standalone binary (main.cpp)
- **Frontend**: Flutter/Dart application using `libface_backend.so` through a C-ABI FFI bridge
- **Database**: SQLite3 with tables for persons, embeddings, attendance, cameras, zones, etc.

### ML Models Used (TensorRT FP16):
1. **Face Detection**: `det_500m_FP32` (640x640 input, 5 landmark points output)
2. **Head Detection**: `headdetection_v12` (YOLO-based, 640x640)
3. **Face Recognition**: `webface_r50` (112x112 input, 512-dim embedding output)

---

## 2. Backend Architecture

### Directory Structure

```
backend/
├── CMakeLists.txt              # Build definition
├── conanfile.txt               # Conan dependencies
├── main.cpp                    # Standalone entry point
├── core/
│   ├── config/
│   │   ├── Config.h / .cpp     # FaceConfig class - loads config.json
│   │   ├── config.json         # Main configuration file
│   │   └── license.json        # License file
│   ├── EventBus.h / .cpp       # Event bus system
│   ├── IModule.h               # Module interface
│   ├── PluginRegistry.h/.cpp   # Plugin/module registry
│   ├── SFace.h                 # Core data structure for face detection
│   ├── StaticModule.h/.cpp     # Static module implementation
│   ├── utils.h                 # Utility functions
│   ├── models/                 # ML model files (.engine, .onnx)
│   ├── crypto/                 # Encryption utilities
│   └── license/                # License management
├── modules/
│   ├── attendance/
│   │   ├── AttendanceManager.h/.cpp   # Attendance logic (arrival/departure)
│   │   └── WorkSessionTracker.h/.cpp  # Work session tracking
│   ├── camera/
│   │   ├── CameraManager.h/.cpp       # Camera discovery & management
│   │   ├── CameraDiscovery.h/.cpp     # ONVIF/V4L2 discovery
│   │   └── CameraStreamer.h/.cpp      # Camera streaming
│   ├── detection/
│   │   ├── CDetection.h/.cpp          # Face detection (SCRFD-like)
│   │   ├── CFaceProcess.h/.cpp        # High-level face processing wrapper
│   │   └── head/
│   │       ├── CHead.h/.cpp           # Head detection (YOLO)
│   ├── draw/
│   │   ├── DetectionDrawer.h/.cpp     # Draw detection boxes
│   │   ├── RecognitionDrawer.h/.cpp   # Draw recognition labels
│   │   └── IFrameDrawer.h             # Drawer interface
│   ├── pipeline/
│   │   ├── FacePipeline.h/.cpp        # ** MAIN PIPELINE - Core processing **
│   ├── recognition/
│   │   ├── CRecognition.h/.cpp        # Face recognition (embedding extraction)
│   │   ├── FaceAuthenticator.h/.cpp   # ** FULL AUTHENTICATION ORCHESTRATOR **
│   │   ├── FaceSearcher.h/.cpp        # FAISS-based embedding search
│   │   ├── LocationTracker.h/.cpp     # Person location tracking
│   │   ├── MotionAnalyzer.h/.cpp      # Motion direction analysis
│   │   ├── RecognitionTypes.h         # Shared types
│   │   └── Utils.h                    # Crop & directory utilities
│   └── tracking/
│       ├── CTrack.h/.cpp              # ByteTrack wrapper
│       ├── CFar.h/.cpp                # Distance/size analyzer (IS A FLAG, NOT A GATE)
│       ├── CMotion.h/.cpp             # Motion detection
│       └── CProfile.h/.cpp            # Side profile detection (IS A FLAG, NOT A GATE)
├── services/
│   ├── buffer/                        # Frame buffering
│   ├── camera/
│   │   ├── CameraOrchestrator.h/.cpp  # ** MAIN CAMERA PROCESSING ORCHESTRATOR **
│   ├── drawing/                       # (empty)
│   ├── inference/
│   │   ├── InferenceService.h/.cpp    # Inference service
│   ├── storage/
│   │   ├── FaceDatabase.h/.cpp        # SQLite database (1166 lines)
│   │   └── StorageService.h/.cpp      # Storage service abstraction
│   └── stream/
│       └── CameraPreviewHub.h/.cpp    # Frame preview publishing
├── ffi/
│   ├── face_ffi.h                     # C-ABI header for Flutter
│   └── face_ffi.cpp                   # FFI implementation (1047 lines, ~60 exported functions)
├── migrations/
│   ├── 20260512_add_module_config_and_access_levels.sql
│   └── 20260526_add_stream_enabled_to_cameras.sql
├── plugins/                           # (empty)
├── build/                             # Build output
└── data/                              # Crop images directory
```

### Build System

```cmake
# CMakeLists.txt key points:
# - C++20 standard
# - Produces SHARED library: libface_backend.so
# - Produces executable: FaceAuthentication (links to face_backend)
# - Dependencies: OpenCV 4.12.0, TensorRT, byte_track, faiss, SQLite3, etc.

# conanfile.txt key dependencies:
opencv/4.12.0
byte-track/1.0.0-2-71f99ef7
nlohmann_json/3.11.3
tensorrt-runtime/test-0.1     # Custom Conan package
sqlite3/3.53.0
faiss/1.12.0                  # For embedding similarity search
gstreamer/1.28.1              # Camera streaming
pugixml/1.15                  # XML parsing (ONVIF)
```

---

## 3. Database Schema (SQLite)

### Tables:

```sql
-- Persons (known + unknown registration)
persons(id, name, code, created_at, is_known, picture_path, access_level)

-- Reference embeddings for known persons
reference_faces(id, person_id, embedding BLOB, created_at)

-- Unknown faces detected but not identified
unknown_faces(id, embedding BLOB, track_id, camera_uid, timestamp, image_path)

-- Attendance events
attendance_logs(id, person_id, track_id, event_type, timestamp)

-- All raw detections (both known and unknown)
raw_detections(id, person_id, track_id, embedding BLOB, image_path, timestamp, status)

-- System settings
system_settings(key PRIMARY KEY, value, description, updated_at)

-- Person work schedules
person_schedules(id, person_id UNIQUE, expected_arrival, expected_departure, grace_minutes, work_days)

-- Zones with access control
zones(id, name, description, created_at, required_access_level)

-- Camera configurations
cameras(id, camera_uid UNIQUE, name, zone_id, type, stream_enabled, ai_enabled,
        connection_type, username, password, stream_uri, encoding, onvif_xaddrs, transport_protocol)

-- Per-camera module configuration
camera_module_config(camera_uid, module_name, enabled, config_json)

-- Work sessions
work_sessions(id, person_id, date, arrival_time, departure_time, total_work_seconds, total_break_seconds, is_complete)

-- Break records
breaks(id, session_id, person_id, break_start, break_end, break_seconds)

-- Location tracking
location_log(id, person_id, zone_id, camera_id, timestamp)

-- Tardiness records
tardiness_log(id, person_id, date, expected_arrival, actual_arrival, tardiness_seconds, is_absent)
```

---

## 4. Processing Paths

There are **THREE SEPARATE processing paths** in this project. They all share `FacePipeline::ProcessFrame()` but handle the results differently.

### Path A: CameraOrchestrator (main.cpp — Standalone Binary)

**File**: `services/camera/CameraOrchestrator.cpp`
**Called from**: `main.cpp:142` → `orchestrator->start()`

This is the **primary production path** for standalone operation.

```
CameraOrchestrator::start()
  → startWorker() for each enabled camera
    → workerLoop() (dedicated thread per camera)
      → streamer->getLatestFrame(frame)
      → pipeline->ProcessFrame(frame)         ← FacePipeline
      → matchDetections(cameraUid, detections)  ← FAISS search + Attendance
      → persistDetections(cameraUid, detections, frame)  ← DB save + crop save
      → previewHub_->publishFrame()             ← JPEG for Flutter preview
```

**matchDetections()** (line 321-389):
- Creates its OWN `FaceSearcher` instance (separate from FaceAuthenticator's)
- Also receives `AttendanceManager` from outside
- Iterates detections with embeddings, does FAISS search
- If recognized (personId >= 0): updates `detection.mPersonId`, calls `attendanceMgr_->markArrival/Departure`
- If NOT recognized (personId = -1): sets `detection.mPersonId = -1`, `detection.mDisplayName = "Unknown"`

**persistDetections()** (line 421-468):
- Logs ALL detections to `raw_detections` table
- If recognized: removes from `unknown_faces` table (by track_id + camera_uid)
- If UNKNOWN:
  - First sighting: `saveCroppedFace()` + `addUnknownFace()` 
  - Subsequent sightings: **`updateUnknownFaceEmbedding()`** — UPDATES the embedding with newer frame data
  - **No guarantee that the newer frame is better quality** than the first one

### Path B: FaceAuthenticator (FFI for Flutter — NOT currently used from FFI)

**File**: `modules/recognition/FaceAuthenticator.cpp`
**Created via**: `ffi/face_ffi.cpp` `face_init()` → `CameraOrchestrator` is created, not FaceAuthenticator directly

This class has its own:
- `FacePipeline` instance (per camera in `CameraRuntime`)
- `FaceSearcher` instance
- `AttendanceManager` instance
- `MotionAnalyzer` instance
- `LocationTracker` instance
- `FaceDatabase` instance
- Full `processDetection()` method for handling per-detection results

**processFrame()** (line 66-100):
- Calls `pipeline->ProcessFrame(frame)`
- For each detection WITH embedding AND face:
  - Calls `processDetection()`

**processDetection()** (line 205-369):
- Calls `identify(embedding)` → FAISS search with distance threshold
- If UNKNOWN (personId == -1):
  - Uses `static std::unordered_set<std::string> unknownCropped` to ensures **ONLY ONE** crop per trackId
  - Saves crop + `addUnknownFace()` only on first sighting
  - Tracks arrival/departure direction for unknown persons
- If KNOWN (personId >= 0):
  - Updates location
  - Calls `attendanceMgr_->markArrival/Departure` based on camera type and direction

**IMPORTANT**: This `processFrame()` method is **NOT CALLED from any FFI function**. The FFI only exposes `face_process_frame_raw()` which directly calls `pipeline->ProcessFrame()` without going through FaceAuthenticator. FaceAuthenticator appears to be a legacy/orphaned class or was designed for a different integration path.

### Path C: Direct FFI Pipeline (Flutter Live Camera Screen)

**File**: `ffi/face_ffi.cpp:202` → `processCvMat()`
**Called from**: Flutter `live_camera_screen.dart` via `service.processFrame(pipeline, bgrData, ...)`

```
Flutter LiveCameraScreen (66ms timer)
  → service.openCamera(uri)              ← face_camera_open()
  → service.createPipeline()             ← face_pipeline_create()
  → service.getCameraFrameBgr()          ← face_camera_get_frame_bgr()
  → service.processFrame(pipeline, ...)  ← face_process_frame_raw()
    → PipelineHolder::pipeline->ProcessFrame(frame)
    → extractEmbeddingSync() for detections without async embeddings yet
    → Returns JSON with: track_id, embedding, is_side_profile, is_far, bbox
  → FaceOverlayPainter (draws boxes on image)
```

This path:
1. Opens its own camera streamer (separate from CameraOrchestrator)
2. Creates its own FacePipeline (separate from any other path)
3. **Does NOT persist anything to database**
4. **Does NOT do any FAISS search or identification**
5. **Does NOT save crops or unknown_faces**
6. Is purely for **live preview/display purposes**

---

### Path D: PreviewHub (Flutter + CameraOrchestrator Combined)

**File**: `services/stream/CameraPreviewHub.cpp` + `attendance_service.dart`

```
Flutter Settings/other screens
  → service.subscribeCameraPreview(uid)
    → face_preview_subscribe() → CameraOrchestrator::notifyPreviewSubscriptionsChanged()
      → CameraOrchestrator starts worker for that camera (if not already running)
        → workerLoop() runs Path A (ProcessFrame → matchDetections → persistDetections)
  
  → service.getLatestCameraPreview(uid) ← JPEG with drawn boxes
```

This path uses the CameraOrchestrator which already has:
- FacePipeline processing
- FAISS identification
- Attendance tracking
- Database persistence
- AND sends processed frames back to Flutter as JPEG

---

## 5. Flutter Frontend

### Structure

```
attendance_app/
├── lib/
│   ├── main.dart                      # App entry with routing
│   ├── screens/
│   │   ├── add_camera_screen.dart     # Manual camera addition
│   │   ├── add_person_screen.dart     # Person enrollment
│   │   ├── attendance_screen.dart     # Attendance view
│   │   ├── camera_search_screen.dart  # Camera discovery
│   │   ├── camera_settings_dialog.dart
│   │   ├── camera_settings_screen.dart
│   │   ├── cameras_screen.dart        # Camera list
│   │   ├── dashboard_screen.dart      # Main dashboard
│   │   ├── ffi_test_screen.dart       # FFI test/debug screen
│   │   ├── live_camera_screen.dart    # ** LIVE CAMERA VIEW **
│   │   ├── module_config_dialog.dart
│   │   ├── persons_screen.dart        # Person management
│   │   ├── reports_screen.dart        # Reports & analytics
│   │   ├── settings_screen.dart       # Settings
│   │   ├── workers_screen.dart        # Worker list
│   │   └── zones_screen.dart          # Zone management
│   ├── services/
│   │   └── attendance_service.dart    # All FFI calls (914 lines)
│   ├── widgets/
│   │   ├── face_overlay_painter.dart  # Draws bbox + track_id overlay
│   │   ├── app_filter_bar.dart
│   │   ├── app_pagination.dart
│   │   ├── app_search_field.dart
│   │   ├── app_section_header.dart
│   │   ├── app_table.dart
│   │   ├── custom_app_bar.dart
│   │   ├── right_side_bar.dart
│   │   └── sidebar.dart
│   └── ...

attendance_bridge/
├── lib/
│   ├── attendance_bridge.dart                  # Dart FFI bindings class (250 lines)
│   ├── attendance_bridge_bindings_generated.dart  # Generated C-ABI bindings
│   └── example/lib/main.dart                   # Example usage
```

### Key Flutter ↔ C++ FFI Functions

The `face_ffi.cpp` exports ~60 C-ABI functions. Key ones:

```c
// Lifecycle
int face_init(const char* config_path, const char* db_path);
void face_cleanup();

// Pipeline (for live camera screen)
FacePipelineHandle face_pipeline_create();
void face_pipeline_destroy(FacePipelineHandle);
char* face_process_frame_raw(FacePipelineHandle, const uint8_t* bgr, int rows, int cols, int step);
char* face_process_frame(FacePipelineHandle, const uint8_t* jpeg, size_t len);

// Database CRUD
FaceDatabaseHandle face_db_open(const char* path);
// ... 40+ functions for persons, cameras, zones, embeddings, unknown_faces, attendance, settings, reports

// Camera Management
CameraManagerHandle face_camera_manager_create();
char* face_camera_discover(CameraManagerHandle, int deep_search);
CameraStreamerHandle face_camera_open(CameraManagerHandle, const char* uri);
uint8_t* face_camera_get_frame_bgr(CameraStreamerHandle, int* rows, int* cols, size_t* step);

// Preview Hub
int face_preview_subscribe(const char* camera_uid);
int face_preview_unsubscribe(const char* camera_uid);
uint8_t* face_preview_get_latest_frame_jpeg(const char* camera_uid, ...);
uint8_t* face_preview_get_latest_frame_bgr(const char* camera_uid, ...);

// Attendance Manager
AttendanceManagerHandle face_attendance_create(FaceDatabaseHandle, const char* defaults_json);
char* face_attendance_mark_arrival(...);
char* face_attendance_mark_departure(...);
```

### How Flutter Uses the Backend (Two Approaches)

**Approach 1 — Direct Pipeline (`live_camera_screen.dart`)**:
- Opens camera directly via `face_camera_open()`
- Creates own pipeline via `face_pipeline_create()`
- Timer loop: get frame → process frame → display
- **Identified issue**: Calls `createPipeline()` which doesn't go through config modules. The `FacePipeline` constructor also calls `EmbeddingWorker` which starts an async thread. This path does NOT do identification or persistence.

**Approach 2 — PreviewHub (via `CameraOrchestrator`)**:
- Subscribes to camera preview via `face_preview_subscribe(uid)`
- CameraOrchestrator starts worker for that camera
- Worker processes frames through full pipeline (detection → recognition → persistence)
- Processed frames (with drawn boxes) are published back as JPEG for Flutter to display
- **This path DOES identification and persistence**

---

## 6. Current Face Processing Flow (FacePipeline::ProcessFrame)

**File**: `modules/pipeline/FacePipeline.cpp:32`

```cpp
ProcessFrameResult FacePipeline::ProcessFrame(cv::Mat& frame) {
    // Step 1: Get face detections with landmarks
    auto detections = face_process_.GetBoxes(frame);   // CDetection::Inference()
    
    // Step 2: Get head detections
    auto headDetections = DetectHeads(frame);           // CHead::Inference()
    
    // Step 3: Merge head detections with face detections (by IoU)
    // Non-overlapping head detections added as SFace{face=false}
    
    // Step 4: ByteTrack tracking
    auto removedTargets = tracker_.Track(detections);   // if tracking_enabled
    
    for (auto& detection : detections) {
        // Step 5: Side profile check (landmark-based)
        if (detection.face && detection.mKeypoint.size() >= 5) {
            detection.mSideProfileView = profile_analyzer_.CheckSideProfile(detection.mKeypoint);
            // ^^ ONLY FLAGS — does NOT prevent anything
        }
        
        // Step 6: Distance check
        detection.mFarDistance = far_analyzer_.CheckSize(frameInfo, detection.mBbox);
        // ^^ ONLY FLAGS — does NOT prevent anything
        
        // Skip non-face detections
        if (!detection.face) continue;
        
        // Step 7: Check if already recognized (cache)
        if (recognized_track_ids_.contains(detection.mTrackID)) {
            detection.mEmbedding = embedding_cache_[detection.mTrackID];
            detection.mHasEmbedding = true;
        }
        
        // Step 8: If not recognized → ENQUEUE for embedding extraction
        if (recognition_enabled_ && !alreadyRecognized) {
            EnqueueEmbeddingTask(detection, frame);
            // ^^ NO QUALITY CHECK BEFORE THIS POINT
        }
    }
}
```

### EmbeddingWorker (async thread)

```cpp
void FacePipeline::EmbeddingWorker() {
    // Pops from queue, extracts embedding:
    auto embedding = face_process_.GetEmbeddings(task.detection, task.frame);
    
    // Caches embedding + marks track as "recognized"
    embedding_cache_[task.track_id] = embedding;
    recognized_track_ids_[task.track_id] = true;
}
```

### EnqueueEmbeddingTask

```cpp
void FacePipeline::EnqueueEmbeddingTask(const SFace& detection, const cv::Mat& frame) {
    if (detection.mTrackID.empty() || !recognition_enabled_) return;
    
    if (recognized_track_ids_.count(detection.mTrackID) &&
        recognized_track_ids_[detection.mTrackID]) return;  // Already done
    
    for (const auto& task : embedding_queue_) {
        if (task.track_id == detection.mTrackID) return;    // Already queued
    }
    
    if (embedding_queue_.size() >= embedding_queue_max_size_) return;  // Queue full
    
    // Create task and push
    SEmbeddingTask task;
    task.track_id = detection.mTrackID;
    task.detection = detection;
    task.frame = frame.clone();
    embedding_queue_.push_back(std::move(task));
    queue_cv_.notify_one();
}
```

### Current Behavior Summary

| Scenario | What Happens | Issue |
|---|---|---|
| **Known person, first frame** | Embedding extracted, cached ✓ | — |
| **Known person, subsequent frames** | Cache hit, no new embedding ✓ | — |
| **Unknown person, first frame** | Embedding extracted, cached as "recognized" (confusing name — means "processed"), FAISS returns no match, face crop saved to DB | **Crop may be low quality** |
| **Unknown person, subsequent frames** | Still "recognized" (cached), same embedding reused, each frame may update `unknown_faces.embedding` (if via CameraOrchestrator path) | **Update may use low quality frame** |
| **Tracking lost then regained** | New track_id → same face treated as new unknown | — |

---

## 7. Identified Problems

### Problem 1: No Quality Gate Before Embedding Extraction

**Location**: `FacePipeline::ProcessFrame()` around line 110, before `EnqueueEmbeddingTask()`

The pipeline currently sends EVERY face detection for embedding extraction regardless of:
- **Blur**: No detection at all
- **Side profile**: `mSideProfileView` is computed but NEVER CHECKED as a gate condition
- **Face too far**: `mFarDistance` is computed but NEVER CHECKED as a gate condition
- **Bad angle**: No yaw/pitch/roll estimation exists (only landmark-based profile detection)

**Result**: The first frame that produces a face detection (even if blurry, side-facing, or far) is used to create the embedding. This is especially problematic for unknown persons because:
- On the CameraOrchestrator path: the first low-quality embedding + crop gets saved to `unknown_faces`
- If `updateUnknownFaceEmbedding()` is called, it updates with potentially another low-quality frame
- The system has no way to prefer a high-quality frame over a low-quality one

### Problem 2: Two Parallel Systems with Duplicate Logic

**CameraOrchestrator** and **FaceAuthenticator** both:
- Wrap `FacePipeline`
- Do FAISS search
- Handle attendance
- Handle unknown faces
- Track motion

But they're **separate instances** with separate state. FaceAuthenticator is currently unused from FFI, creating confusion.

### Problem 3: Confusing "recognized" Naming

In `FacePipeline`:
```cpp
recognized_track_ids_[track_id] = true;
```
This doesn't mean the person was identified/recognized against the gallery. It means "embedding has been extracted and cached for this track_id". A better name would be `embedding_extracted_track_ids_` or `processed_track_ids_`.

This causes: once an unknown person's embedding is cached, `alreadyRecognized = true` → no new embeddings extracted → the original (potentially low-quality) embedding persists.

### Problem 4: Static Set for Unknown Crop Dedup

In `FaceAuthenticator::processDetection()`:
```cpp
static std::unordered_set<std::string> unknownCropped;
```
This `static` set persists across ALL camera runtimes and NEVER clears. If a track_id reappears (e.g., after tracker loss + regain), the set still contains the old track_id, so no new crop is saved even though it's a new sighting.

### Problem 5: Flutter Live Camera Screen Uses Direct Pipeline (No Recognition)

The `live_camera_screen.dart` creates its own pipeline and processes frames directly. It:
- Does NOT call `FaceAuthenticator::processFrame()` or `processDetection()`
- Does NOT do FAISS search
- Does NOT persist anything
- Only displays bounding boxes

This path is essentially a viewer-only mode.

### Problem 6: FFI Test Screen Has Hardcoded Path

In `attendance_service.dart:695`:
```dart
final dylib = ffi.DynamicLibrary.open(
  '/home/mohamad/dev/projects/FaceAuthentication/backend/build/libface_backend.so',
);
```
This path is hardcoded to a developer's machine.

### Problem 7: No Blur/Quality Metrics Captured

The `SFace` struct has `mSideProfileView` and `mFarDistance` fields, but:
- No `mBlurry` or `mQuality` field
- No blur detection anywhere
- No quality score for embedding selection

---

## 8. Existing Code Not Being Used

### Gate Conditions Not Applied

In `FacePipeline::ProcessFrame()`, the following checks are performed but their results are NOT used to gate embedding extraction:

1. **`mSideProfileView`** (line 90): Set from `CProfile::CheckSideProfile()` using 5 landmarks (nose, eyes, mouth corners). Checks if face is in profile view.

2. **`mFarDistance`** (line 92): Set from `CFar::CheckSize()`. Checks if face bbox is too small relative to frame size.

These flags are ONLY:
- Passed to Flutter in JSON response (`face_ffi.cpp:212-213`)
- Available for drawing (but not currently used in any drawer)

### How to Activate as Gates

The condition should be inserted at `FacePipeline.cpp` around line 110:
```cpp
if (recognition_enabled_.load() && !alreadyRecognized) {
    // --- QUALITY GATE: INSERT HERE ---
    // Check: not side profile, not too far, not blurry
    if (!detection.mSideProfileView && !detection.mFarDistance && !isBlurry) {
        EnqueueEmbeddingTask(detection, frame);
    }
    // If rejected by gate → wait for next frame (new frame for same track_id)
}
```

### Other Unused Components

1. **`plugins/`** directory is empty
2. **`services/drawing/`** directory is empty (drawing is in modules/draw/)
3. **`FaceAuthenticator::processFrame()`** is not called from anywhere except possibly internally
4. **`getUnknownDetectionsNotProcessed()`** returns empty — TODO

---

## 9. The AGENT_BRIEF Document

**File**: `AGENT_BRIEF_HeadTrack_Pipeline.md`

This is a **separate design document** for a different pipeline architecture (Python-based). It describes:

- **Head Detector** (YOLOv10n) → **ByteTrack** → **WHENet Head Pose** → **Yaw Gate (45°)** → **Face Aligner** → **AdaFace Recognition**
- The Yaw Gate concept: `abs(yaw) < 45°` → run face recognition
- Identity Cache: once identified, skip recognition for that track

This is **NOT implemented** in the current C++ code. The current C++ code has:
- A simple head detector (YOLOv5/v8-based, not YOLOv10n)
- No head pose estimation (WHENet or similar)
- No yaw gate
- No proper identity cache (current "recognized" flag prevents re-extraction but doesn't cache identity)

However, the **concept** is directly applicable to the current problem:
- The current landmark-based `CProfile::CheckSideProfile()` approximates what the yaw gate does
- Adding blur detection + using existing flags as gates would solve the problem without needing the full WHENet pipeline

---

## 10. Configuration System

**File**: `core/config/config.json`

```json
{
  "models": {
    "detection_onnx": "core/models/det_500m_FP32.onnx",
    "detection_engine": "core/models/det_500m_FP32.engine",
    "recognition_onnx": "core/models/webface_r50.onnx",
    "recognition_engine": "core/models/webface_r50.engine",
    "head_onnx": "core/models/headdetection_v12.onnx",
    "head_engine": "core/models/headdetection_v12.engine"
  },
  "thresholds": {
    "detection": 0.4,
    "head": 0.6,
    "far_height": 0.036,
    "far_width": 0.017,
    "head_face_iou": 0.4
  },
  "tracker": {
    "track_buffer": 30,
    "track_threshold": 0.5,
    "high_threshold": 0.6,
    "match_threshold": 0.8
  },
  "motion": {
    "area_threshold": 1000,
    "buffer_frames": 5,
    "diff_threshold": 20,
    "blur_kernel_size": 5,
    "dilate_kernel_size": 5
  },
  "pipeline": {
    "embedding_queue_max_size": 128
  },
  "database": {
    "path": "attendance.db",
    "crops_dir": "data/crops"
  },
  "recognition": {
    "faiss_threshold": 0.6,
    "motion_window_size": 10,
    "motion_threshold_px": 15.0
  },
  "attendance": {
    "default_arrival": "08:00",
    "default_departure": "17:00",
    "late_grace_minutes": 15
  },
  "modules": {
    "detection": { "enabled": true },
    "tracking": {
      "enabled": true,
      "motion_short_circuit": false,
      "motion_threshold": 1000,
      "motion_buffer": 5,
      "motion_diff_threshold": 20,
      "blur_kernel_size": 5,
      "dilate_kernel_size": 5
    },
    "recognition": {
      "enabled": true,
      "faiss_threshold": 0.3,
      "motion_window_size": 10,
      "motion_threshold_px": 15.0
    },
    "liveness": { "enabled": false },
    "attendance": { "enabled": true }
  }
}
```

**Note**: `recognition.faiss_threshold` appears twice — in `recognition` section (0.6) and in `modules.recognition` (0.3). The code can override these via per-camera module configs in the database.

**Config class** (`Config.h`/`Config.cpp`): Loads from JSON file, resolves relative paths, provides getters. Also supports `Reload()` for runtime reconfiguration. Module parameters can be overridden per-camera via `camera_module_config` DB table (accessed through `FaceAuthenticator::reloadRuntimeSettings()`).

---

## 11. SFace (Core Data Structure)

**File**: `core/SFace.h`

```cpp
struct SFace {
    std::string mTrackID{};              // ByteTrack tracker ID
    int64_t mPersonId{-1};               // Database person ID (-1 = unknown)
    std::string mDisplayName{};          // "Unknown" or person name
    float mRecognitionConfidence{0.0F};  // 1.0 - cosine_distance

    float mConfidence{};                 // Detection confidence
    cv::Rect mBbox{};                    // Bounding box
    bool face=true;                      // true = face detection, false = head-only
    std::vector<cv::Point> mKeypoint{};  // 5 landmarks (eyes, nose, mouth corners)
    FeatureArray mEmbedding{};           // 512-dim float embedding
    bool mHasEmbedding{false};           // Whether embedding has been computed

    cv::Mat mFaceImage{};                // Face crop image (usually empty)

    bool mSideProfileView{};             // Computed but NOT USED as gate
    bool mFarDistance{};                 // Computed but NOT USED as gate
};
```

---

## 12. Key Files Reference

| File | Line Count | Purpose |
|---|---|---|
| `modules/pipeline/FacePipeline.cpp` | 253 | **Core pipeline — where quality gate should go** |
| `modules/pipeline/FacePipeline.h` | 86 | Pipeline class declaration |
| `services/camera/CameraOrchestrator.cpp` | 470 | **Main production processing path** |
| `services/camera/CameraOrchestrator.h` | 76 | Orchestrator class declaration |
| `modules/recognition/FaceAuthenticator.cpp` | 681 | **Full authenticator (currently isolated)** |
| `modules/recognition/FaceAuthenticator.h` | 129 | Authenticator class declaration |
| `ffi/face_ffi.cpp` | 1047 | **FFI bridge — all C-ABI exports for Flutter** |
| `services/storage/FaceDatabase.cpp` | 1166 | **SQLite database operations** |
| `services/storage/FaceDatabase.h` | 131 | Database class declaration |
| `modules/detection/CFaceProcess.cpp` | 47 | Face detection + recognition wrapper |
| `modules/detection/CDetection.cpp` | 217 | Face detector (SCRFD-like) |
| `modules/recognition/CRecognition.cpp` | 119 | Face recognition (embedding extraction) |
| `modules/detection/head/CHead.cpp` | 209 | Head detector (YOLO) |
| `modules/tracking/CProfile.cpp` | 82 | **Side profile detection (not used as gate)** |
| `modules/tracking/CFar.cpp` | ~20 | **Distance detection (not used as gate)** |
| `core/config/Config.cpp` | 250 | Configuration loader |
| `core/config/Config.h` | 118 | Config class declaration |
| `core/SFace.h` | 34 | Core data structure |
| `main.cpp` | 188 | Standalone entry point |
| `AGENT_BRIEF_HeadTrack_Pipeline.md` | 447 | Design doc for different Python pipeline |
| Flutter: `attendance_service.dart` | 914 | All FFI calls from Dart |
| Flutter: `live_camera_screen.dart` | 219 | Direct pipeline camera view |
| Flutter: `face_overlay_painter.dart` | 49 | Draws bbox + track_id on camera feed |

---

## Summary of What Needs To Be Done

The core task is to add a **Quality Gate** before embedding extraction in `FacePipeline::ProcessFrame()`. This gate should check:

1. **Not side profile** (`mSideProfileView == false`) — already computed
2. **Not too far** (`mFarDistance == false`) — already computed
3. **Not blurry** — needs to be implemented (Laplacian Variance via OpenCV)
4. **Face size sufficient** — already partially covered by CFar
5. **Frontal face** — landmark-based or head pose (optional enhancement)

The gate should be inserted at `FacePipeline.cpp:110` before `EnqueueEmbeddingTask()`.

Additionally:
- Add `mBlurry` / `mQualityScore` fields to `SFace` struct
- Add quality config parameters to `config.json` (blur_threshold, quality_gate_enabled)
- Pass quality info to Flutter in JSON response
- Fix static set issue in FaceAuthenticator
- Clarify naming of `recognized_track_ids_`
- Ensure CameraOrchestrator's `persistDetections()` prefers higher-quality embeddings when updating

---

*End of Analysis Document*

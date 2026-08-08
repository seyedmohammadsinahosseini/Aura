# Face Authentication & Attendance System

A real-time, multi-camera face authentication and attendance management backend
built in modern C++ (C++20). The system discovers IP/USB cameras (ONVIF, RTSP,
V4L2), streams video through a GStreamer/OpenCV pipeline, detects faces and head
positions with TensorRT-accelerated deep-learning models, tracks individuals
with ByteTrack, recognizes them against a FAISS embedding index, and records
arrival/departure attendance into SQLite.

The project exposes both a REST/HTTP admin API (with live MJPEG previews) and a
C FFI library so the recognition backend can be embedded in or called from other
applications and UIs.

---

## Purpose

This project was built as a production-oriented engineering exercise and is
published as a public portfolio piece. It demonstrates:

* System-level C++ engineering: concurrency, threading, socket programming,
  plugin architecture, and hardware integration.
* GPU-accelerated deep-learning inference with NVIDIA TensorRT.
* A clean separation between a C++ recognition core, a C FFI boundary, and an
  HTTP service layer.
* Integration of a wide set of native libraries via the Conan package manager.

---

## Features

**Camera handling**
* Local camera capture through Linux V4L2 (`/dev/video*`).
* Network camera discovery: ONVIF WS-Discovery, ONVIF metadata probing, and an
  optional RTSP port/path scan for deep search.
* ONVIF PTZ control and ONVIF client helpers (device/media services, streaming
  profiles).
* GStreamer/OpenCV based streaming with per-camera profiles (encoding, width,
  height, FPS) and RTSP/FFmpeg fallback.

**Vision pipeline**
* Face detection via a TensorRT-accelerated detection model (multi-scale
  keypoint head), head detection, and face/head association.
* Motion analysis (background-difference motion detection with configurable
  thresholds, blur and dilation) used to short-circuit expensive inference.
* Object tracking with the ByteTrack algorithm (Kalman filter + Hungarian
  matching), wrapped in `core` tracking modules.
* Face recognition using a ResNet-50 style embedding model and a FAISS
  in-memory index (`IndexFlatIP`) for fast nearest-neighbour search.
* Configurable quality gates: blur gate, side-profile gate, and far-distance
  gate.
* Per-camera and per-module enable/disable plus runtime module configuration.

**Attendance**
* Arrival/departure marking keyed by person + track ID.
* Work-session tracking, daily summaries, timelines, tardiness lists, and
  current-location reporting.
* Per-person schedules and access levels, plus zone-based access-level checks.

**Persistence & API**
* SQLite storage for persons, reference embeddings, unknown faces, raw
  detections, cameras, zones, schedules, settings, and attendance records.
* SQL migrations under `migrations/`.
* REST admin API with dashboard statistics/activity, camera management, person
  management, unknown-face review (enroll/merge), attendance reporting, and
  settings — plus live MJPEG camera previews.
* C FFI (`ffi/face_ffi`) exposing the pipeline, database, attendance, camera,
  and preview-hub functionality to other languages.

---

## Architecture

```
  Cameras (V4L2 / ONVIF / RTSP)
        │
        ▼
  CameraManager / CameraStreamer / OnvifClient        (modules/camera)
        │  frames
        ▼
  CameraPreviewHub  ──►  HTTP server (MJPEG)          (services/stream, http/)
        │  frames
        ▼
  CameraOrchestrator                                   (services/camera)
        │
        ▼
  FacePipeline (detect ▸ track ▸ recognize)           (modules/pipeline)
        │
        ├── CDetection / CHead            ── TensorRT engines (modules/detection)
        ├── CFar / CMotion / CTrack       ── ByteTrack         (modules/tracking)
        ├── CRecognition / FaceAuthenticator ── FAISS + FaceSearcher (modules/recognition)
        └── AttendanceManager / WorkSessionTracker             (modules/attendance)
        │
        ▼
  FaceDatabase (SQLite)  +  crops/ face images          (services/storage)
        │
        ▼
  C FFI layer (ffi/face_ffi)  ──►  HTTP admin API (http/)  ──►  Dashboard UI
```

Key design points:

* **Module/plugin registry** — `core/PluginRegistry` loads feature modules
  (detection, tracking, recognition, quality gate, liveness, attendance) and
  honors an entitlement/license set at load time.
* **FFI boundary** — the shared library exposes a stable C API
  (`ffi/face_ffi.h`) so the backend can be driven from non-C++ hosts.
* **Preview hub** — a per-camera frame cache (latest JPEG/BGR) that decouples
  capture, processing, and serving.
* **Pipeline** — frame-level processing with an embedding queue used for
  recognition.

---

## Technologies

| Area | Technology |
|------|-----------|
| Language | C++20 |
| Build system | CMake ≥ 3.26, Conan 2.x package manager |
| GPU inference | NVIDIA CUDA, TensorRT, custom `tensorrt-runtime` wrapper |
| Vision | OpenCV 4.x |
| Tracking | ByteTrack (custom Conan package `byte-track`) |
| Similarity search | FAISS (`IndexFlatIP`) |
| Streaming | GStreamer, FFmpeg (via OpenCV backends) |
| Media capture | V4L2 (Linux), RTSP, ONVIF (SOAP/XML) |
| Persistence | SQLite 3 |
| HTTP | cpp-httplib (single header) |
| Serialization | nlohmann/json |
| XML (ONVIF) | pugixml |
| Logging | custom `log-factory` / `log-entry` packages |

---

## Directory structure

```
.
├── CMakeLists.txt              # Top-level CMake build
├── conanfile.txt               # Conan dependencies
├── conan-custom/               # Local Conan recipes/packages
│   ├── byte-track/             # ByteTrack tracking library
│   ├── log-factory/            # Logging factory (headers)
│   ├── log-entry/              # Log entry types (proprietary — not distributed)
│   └── tensorrt-runtime/       # TensorRT inference wrapper
├── core/
│   ├── config/                 # Configuration loader + JSON config
│   ├── crypto/                 # SHA-1 (ONVIF WS-Security)
│   ├── license/                # License/entitlement manager
│   ├── models/                 # Model notes + conversion scripts (weights NOT included)
│   ├── xml/                    # XML parser wrapper (ONVIF)
│   ├── EventBus.h/cpp          # Lightweight event bus
│   ├── PluginRegistry.h/cpp    # Feature-module registry
│   ├── SFace.h                 # Core detection/recognition result type
│   └── StaticModule.h/cpp      # Built-in module base
├── modules/
│   ├── attendance/             # Attendance manager, work-session tracker
│   ├── camera/                 # Discovery, manager, streamer, ONVIF client/PTZ
│   ├── detection/              # TensorRT face/head detectors
│   ├── draw/                   # Detection / HUD / recognition drawers
│   ├── pipeline/               # Frame-processing pipeline
│   ├── recognition/            # FaceAuthenticator, FaceSearcher, motion, location
│   └── tracking/               # ByteTrack wrappers (far/motion/profile/track)
├── services/
│   ├── buffer/                 # Safe circular buffer
│   ├── camera/                 # Camera orchestrator
│   ├── drawing/                # Drawing helpers
│   ├── inference/              # Generic inference service
│   ├── storage/                # SQLite FaceDatabase, storage service
│   └── stream/                 # Camera preview hub
├── ffi/
│   ├── face_ffi.h              # C API header
│   └── face_ffi.cpp            # C API implementation
├── http/
│   ├── httplib.h               # cpp-httplib (vendored, single header)
│   ├── http_server.h/cpp       # REST API + MJPEG streaming server
├── migrations/                 # SQL migrations (applied by the app)
├── main.cpp                    # Full application entry point
└── main_admin_server.cpp       # Admin API server entry point
```

---

## Requirements

The project targets **Linux (Ubuntu 22.04 / 24.04) with NVIDIA GPU support**.
The code uses Linux-only headers (`linux/videodev2.h`, `ifaddrs.h`, `getifaddrs`,
`localtime_r`, …), so Windows/macOS are not supported.

* Ubuntu 22.04+ (64-bit)
* NVIDIA GPU with a driver compatible with your CUDA/TensorRT versions
* **CUDA Toolkit** (CUDA 12.x recommended)
* **TensorRT** (8.x / 10.x) — the app loads TensorRT `.engine` files
* **OpenCV** 4.x (with GStreamer, FFmpeg, V4L support)
* **FAISS** (CPU build, `IndexFlatIP`)
* **GStreamer** 1.x (runtime for RTSP streaming) with `gstreamer-plugins-base`
  and codec plugins
* **FFmpeg** (optional fallback backend)
* **SQLite 3**
* **Conan 2.x** and **CMake ≥ 3.26**
* A C++20 compiler (GCC ≥ 11 or Clang ≥ 14)

> Note: the vendored `http/httplib.h` may be built with SSL support; the default
> build here uses the plain-HTTP server on a local/trusted network.

---

## Build instructions

### 1. Install system prerequisites

```bash
sudo apt update
sudo apt install -y build-essential cmake python3 python3-pip git \
    libssl-dev libsqlite3-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav
```

Install the NVIDIA CUDA Toolkit and TensorRT (see NVIDIA's official
instructions) and make sure `nvcc`, `trtexec`, and the TensorRT libraries are on
your `PATH`/`LD_LIBRARY_PATH`.

Install FAISS:

```bash
pip install faiss-cpu        # headers are in the system FAISS package
# or: sudo apt install -y libfaiss-dev
```

### 2. Install Conan

```bash
python3 -m pip install conan
conan profile detect
```

### 3. Create the custom Conan packages

The project depends on a few local packages that ship in `conan-custom/`. Build
them with `conan create` before configuring the project:

```bash
conan create conan-custom/tensorrt-runtime/test-0.1
conan create conan-custom/log-factory/1.0.0-1-56934db0
conan create conan-custom/byte-track/1.0.0-2-71f99ef7
```

> **Important:** `tensorrt-runtime` depends on the `log-entry` package. The
> `log-entry` sources are **proprietary and are intentionally not distributed**
> with this repository. You must provide/`conan create` that package separately
> from your own copy before the build will resolve.

### 4. Configure and build

```bash
conan install . --output-folder=build --build=missing
cmake --preset conan-release   # or: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This produces:
* `libface_backend.so` — the shared recognition library (FFI).
* `FaceAuthentication` — the full application binary.
* `admin_server` — the HTTP admin API server binary.

---

## Configuration

The application is configured with a JSON file (default
`core/config/config.json`). A safe, documented example is provided at
`core/config/config.example.json` — copy it and adjust as needed:

```bash
cp core/config/config.example.json core/config/config.json
```

Key settings:

| Section | Purpose |
|---------|---------|
| `models` | Paths to ONNX / TensorRT engine files for detection, recognition, head detection |
| `thresholds` | Detection, head, far-distance, and IOU thresholds |
| `tracker` | ByteTrack buffer and matching thresholds |
| `motion` | Motion-analysis parameters |
| `database` | SQLite DB path and face-crops directory |
| `license` | Path to the license/entitlement file |
| `attendance` | Default work hours and late-grace period |
| `modules.*` | Per-module enable/disable and parameters |

The license file (`core/config/license.json`) is **not** distributed (it holds
entitlement information). A template is provided at
`core/config/license.example.json`:

```bash
cp core/config/license.example.json core/config/license.json
```

Create/adjust it according to your own licensing setup. Without a valid license
file the feature modules will not be loaded.

---

## Model setup

Model weights are **not** committed to this repository. See
[`core/models/README.md`](core/models/README.md) for the full inventory, the
expected filenames, and conversion instructions. In short, the app needs:

```
core/models/det_500m_FP32.onnx      + det_500m_FP32.engine
core/models/webface_r50.onnx        + webface_r50.engine
core/models/headdetection_v12.onnx  + headdetection_v12.engine
```

The `.engine` files are TensorRT engines built with `trtexec`; the ONNX files
are used for conversion/fallback. Respect the licenses of the models you obtain.

---

## How to run

### Full application (camera discovery + processing + API)

```bash
./build/FaceAuthentication [config.json] [attendance.db]
```

The application:
1. Loads configuration and opens the SQLite database.
2. Discovers cameras (local + ONVIF + optional RTSP deep scan) and registers them.
3. Starts the camera orchestrator and preview hub.
4. Starts the HTTP admin API on `0.0.0.0:8080`.
5. Runs headlessly when no `$DISPLAY` is available (recommended for servers), or
   shows camera feeds in a desktop window otherwise.

### Admin API server only (cameras added via the API)

```bash
./build/admin_server [config.json] [attendance.db]
```

This starts the HTTP server and the FFI-initialized orchestrator; cameras are
added later through the API (`POST /api/cameras`) and start automatically.

---

## REST API overview

The HTTP server runs on port **8080** and returns JSON. Select endpoints:

| Method | Path | Description |
|--------|------|-------------|
| GET  | `/api/health` | Health check |
| POST | `/api/init`, `/api/cleanup` | Initialize / release the backend |
| GET/POST | `/api/persons` | List / create persons |
| GET/PUT/DELETE | `/api/persons/{id}` | Read / update / delete a person |
| PUT | `/api/persons/{id}/access-level` | Set person access level |
| GET/PUT | `/api/persons/{id}/schedule` | Get / set person schedule |
| POST | `/api/persons/{id}/face` | Enroll a face image for a person |
| GET | `/api/embeddings/references` | All reference embeddings |
| GET | `/api/files/{path}` | Serve face crop images (restricted to the crops dir) |
| GET | `/api/dashboard/stats` | Dashboard statistics |
| GET | `/api/dashboard/activity` | Recent activity feed |
| GET | `/api/unknown-faces` | Unknown faces awaiting review |
| DELETE | `/api/unknown-faces/{id}` | Remove an unknown face |
| POST | `/api/unknown-faces/{id}/enroll` | Enroll an unknown face as a new person |
| POST | `/api/unknown-faces/{id}/merge` | Merge an unknown face into a person |
| GET/POST | `/api/cameras` | List / add cameras |
| GET/PUT/DELETE | `/api/cameras/{id}` | Manage a camera |
| POST | `/api/cameras/discover` | Run camera discovery |
| GET | `/api/cameras/{id}/stream` | Live MJPEG stream for a camera |
| GET | `/api/cameras/by-uid/{uid}/stream` | Live MJPEG stream by camera UID |
| GET/PUT | `/api/cameras/{id}/modules`, `/api/cameras/{id}/modules/{name}` | Per-camera module config |
| GET/POST/PUT/DELETE | `/api/zones` | Manage access-control zones |
| POST | `/api/attendance/arrival`, `/api/attendance/departure` | Mark arrival/departure |
| GET | `/api/attendance/summary` | Daily summary |
| GET | `/api/attendance/tardiness` | Tardiness list |
| GET | `/api/attendance/timeline` | Timeline for a person/date |
| GET | `/api/events` | Raw detection events |
| GET/PUT | `/api/settings`, `/api/settings/{key}` | Application settings |
| GET | `/api/modules` | Available feature modules |

CORS headers are set for browser-based dashboards.

---

## FFI / library usage

The shared library `libface_backend.so` exports a C API defined in
[`ffi/face_ffi.h`](ffi/face_ffi.h). Example flows:

```c
#include "ffi/face_ffi.h"

// Initialize once
face_init("core/config/config.json", "attendance.db");

// Process a frame (bytes → detections with embeddings as JSON)
FacePipelineHandle p = face_pipeline_create();
char* result = face_process_frame(p, jpeg_bytes, jpeg_len);
// parse `result` (JSON), free with face_free_string
face_pipeline_destroy(p);

// Database access
FaceDatabaseHandle db = face_db_open("attendance.db");
char* persons = face_db_get_all_persons(db);
face_free_string(persons);
face_db_close(db);

face_cleanup();
```

The FFI exposes pipeline processing, database CRUD, camera discovery,
preview-frame retrieval (JPEG/BGR), and attendance marking.

---

## Limitations

* **Linux-only.** The code uses Linux-specific headers and APIs (V4L2, `ifaddrs`,
  `localtime_r`).
* **NVIDIA GPU required** for TensorRT inference; a CPU-only build is not
  supported.
* **No authentication on the HTTP API.** The server is intended for a trusted /
  local network and uses `Access-Control-Allow-Origin: *`. Do not expose it to
  the public internet without adding authentication and TLS.
* **Default camera credential guessing was removed** from the discovery code for
  security reasons; authenticated RTSP streams require credentials to be
  configured by the operator.
* The `log-entry` package is a proprietary dependency and is not redistributed.
* Model weights are not bundled (see [Model setup](#model-setup)).

---

## Security considerations

* Face crop images are served through `/api/files/{path}` only from the
  configured crops directory; path traversal and arbitrary file reads are
  blocked.
* No default/hard-coded camera credentials are shipped in the source.
* The SQLite database, face crops, and reference embeddings are treated as
  private data and are excluded from this public repository.
* Real deployments must add authentication, TLS, and network-level access
  controls, and should rotate/limit the data retained in the database.

---

## Project status

*Active development / portfolio release.* The core pipeline, camera integration,
tracking, recognition, attendance, REST API, and FFI are implemented and were
developed against a real deployment configuration. Model weights and the
proprietary `log-entry` dependency are kept out of this repository.

---

## Future improvements

* Add authentication (e.g., token-based) and HTTPS to the HTTP server.
* Add liveness detection (hook is present, currently disabled by default).
* Add unit/integration test coverage and CI (GitHub Actions) for builds.
* Provide Docker and containerized build recipes.
* Support multiple recognition backends (ONNX Runtime, OpenVINO) in addition to
  TensorRT.
* Publish a reference web dashboard UI alongside the backend.

---

## License

No license is currently specified for this repository; all rights are reserved
by the author. Third-party dependencies (OpenCV, TensorRT, FAISS, GStreamer,
ByteTrack, cpp-httplib, nlohmann/json, pugixml, SQLite, etc.) remain under their
respective licenses, as do the machine-learning models referenced in
[`core/models/README.md`](core/models/README.md). The proprietary `log-entry`
package is not redistributed.

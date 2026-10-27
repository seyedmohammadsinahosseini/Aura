# FaceAuthentication — Project Documentation

> **A real-time GPU-powered face recognition & attendance system**
> Backend: native C++20 / CUDA / TensorRT · Frontend: Flutter Web · Storage: SQLite · Transport: REST over HTTP

---

## Table of Contents

1. [What This Project Is](#1-what-this-project-is)
2. [Architecture Overview](#2-architecture-overview)
3. [Technologies at a Glance](#3-technologies-at-a-glance)
4. [Backend (C++ / CUDA)](#4-backend-c--cuda)
5. [Frontend (Flutter)](#5-frontend-flutter)
6. [Face Recognition Pipeline](#6-face-recognition-pipeline)
7. [REST API Reference](#7-rest-api-reference)
8. [Database](#8-database)
9. [Installation & Setup](#9-installation--setup)
10. [Project Structure](#10-project-structure)
11. [Platforms & Deployment](#11-platforms--deployment)
12. [Configuration](#12-configuration)
13. [Testing & CI](#13-testing--ci)
14. [Security Notes](#14-security-notes)

---

## 1. What This Project Is

FaceAuthentication is a complete face-recognition **attendance and access-control** system:

- **Cameras** (RTSP IP cameras, ONVIF devices, or local webcams) are discovered and streamed.
- A **GPU pipeline** detects faces and heads, tracks people, extracts 512-dimension embeddings, and matches them against a registered gallery (FAISS search).
- Recognized people are marked **arriving / departing** per camera zone; unknown faces are stored for manual review and later enrollment.
- A **Flutter admin panel** manages workers, cameras, zones, attendance reports, and settings — running either as a **web app** (talking REST to the server) or as a **native desktop app** (talking directly to the engine via C FFI).

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                  FLUTTER WEB CONSOLE (attendance_console)           │
│   Riverpod 3 · go_router (URL/deep-links) · Material 3 · Vazirmatn   │
└───────────────────────────┬─────────────────────────────────────────┘
                            │ HTTP REST (port 3000→8080 nginx /api proxy)
                            ▼
┌─────────────────────────────────────────────────────────────────────┐
│                     C++ BACKEND (admin_server)                      │
│  http/http_server.cpp ── REST API (41 routes, cpp-httplib)          │
│           │                                                         │
│           ▼                                                         │
│  services/camera/CameraOrchestrator  (thread per camera)            │
│     ├─► CameraStreamer (V4L2 / RTSP capture thread)                 │
│     ├─► FacePipeline   (detect → track → gates → embed)             │
│     │       ├─ TensorRT: face detection (det_500m)                  │
│     │       ├─ TensorRT: head detection (headdetection_v12)         │
│     │       ├─ ByteTrack: multi-object tracking                     │
│     │       └─ TensorRT: recognition (webface_r50 → 512-d)          │
│     ├─► FaceSearcher   (FAISS IndexFlatIP, 512-dim)                 │
│     ├─► AttendanceManager / WorkSessionTracker                      │
│     └─► CameraPreviewHub (JPEG frames for live preview)             │
│           │                                                         │
│           ▼                                                         │
│  services/storage/FaceDatabase ──► SQLite (attendance.db, 14 tables)│
└─────────────────────────────────────────────────────────────────────┘
```

**Frontend/backend communication:** the web console talks to the REST API over
HTTP — same-origin `/api/` through the nginx proxy in the Docker deployment
(port 3000 → backend on :8080), or a configurable server URL in development
(`Settings → Server URL`). The backend additionally exposes a C FFI
(`backend/ffi/face_ffi.h`, ~134 functions, `libface_backend.so` / `face_backend.dll`)
for embedding the engine in-process; the bundled console does not use it.

---

## 3. Technologies at a Glance

| Layer | Technology |
|---|---|
| **Backend language** | C++20 |
| **Build system** | CMake ≥ 4.0 + Conan 2 (package manager) |
| **GPU inference** | NVIDIA TensorRT + CUDA 12.6.3 + cuDNN |
| **Computer vision** | OpenCV 4.12.0, FFmpeg 4.4.6, GStreamer 1.28.1 |
| **Vector search** | FAISS 1.12.0 (`IndexFlatIP`, 512-dim) |
| **Object tracking** | ByteTrack (custom Conan recipe) |
| **HTTP server** | cpp-httplib 0.54.1 (vendored, REST only) |
| **JSON / XML** | nlohmann_json 3.11.3, pugixml 1.15 (ONVIF SOAP) |
| **Database** | SQLite 3.53.0 (raw C API, no ORM) |
| **Frontend language** | Dart ≥ 3.12 / Flutter ≥ 3.44 (stable) |
| **Frontend state/DI** | Riverpod 3 (no codegen) — feature-first MVVM |
| **Frontend key packages** | `flutter_riverpod`, `go_router`, `fl_chart`, `http`, `shared_preferences`, `shamsi_date` (Jalali), `file_picker` |
| **UI framework** | Material 3, Persian (fa, Jalali + RTL) and English (en), light/dark/system themes, Vazirmatn typeface |
| **Containerization** | Docker + Docker Compose (2-stage CUDA images) |
| **Web server (frontend)** | nginx:alpine (SPA fallback + `/api/` proxy) |
| **CI** | GitHub Actions (Windows desktop build) |

---

## 4. Backend (C++ / CUDA)

### 4.1 Entry points — three build targets

| Target | Source | Purpose |
|---|---|---|
| `admin_server` | `main_admin_server.cpp` | **Production headless REST server** (port 8080) |
| `FaceAuthentication` | `main.cpp` | Native demo with OpenCV display windows |
| `libface_backend.so` | `ffi/face_ffi.cpp` | Shared library with a **C FFI** (~100 `face_*` functions) for embedding in other apps (Windows: `face_backend.dll`) |

**Server start:** `admin_server [configPath] [dbPath] [port]`
Defaults: `core/config/config.json` · `attendance.db` · `8080`

### 4.2 Key libraries

- **TensorRT** (`libnvinfer`, `nvonnxparsers`) — GPU inference engines, cached and shared across camera pipelines.
- **OpenCV** — capture, preprocessing, drawing, JPEG encoding.
- **FAISS** — brute-force inner-product search of 512-d face embeddings against the gallery.
- **ByteTrack** — multi-object tracking to keep stable track IDs per person.
- **pugixml** — ONVIF SOAP / WS-Discovery parsing (camera auto-discovery, PTZ).
- **SQLite3** — all persistence (raw C API via `FaceDatabase`).
- **cpp-httplib** — REST server (JSON, CORS `*`; live preview via JPEG polling, no WebSockets in practice).

### 4.3 Concurrency model

- **One worker thread per camera** (`CameraOrchestrator::workerLoop`): capture → pipeline → match → persist → publish preview.
- **One capture thread per camera** (`CameraStreamer`) feeding a thread-safe circular buffer, with auto-reopen on stream loss.
- **Async embedding worker** (`FacePipeline`): condition-variable queue (max 128) so GPU embedding doesn't block detection/tracking.
- **HTTP thread pool** (4–8 threads); all DB writes serialized under one mutex.
- **Discovery thread pools** (up to 30 threads) for RTSP subnet scanning.

### 4.4 ML models (all in `backend/core/models/`)

| Model | File | Role | Input | Output |
|---|---|---|---|---|
| Face detection | `det_500m_FP32.onnx/.engine` | Face bounding boxes (thr 0.4) | fixed-shape image | boxes + landmarks |
| Head detection | `headdetection_v12.onnx/.engine` | Head boxes, all poses (thr 0.6) | `"images"` tensor | YOLO-style boxes |
| Recognition | `webface_r50.onnx/.engine` | ArcFace-style ResNet-50 embedder | 112×112 aligned crop | **512-d L2-normalized** vector |

Engines are deserialized from `.engine` if present, otherwise built from ONNX (batch 1, 1 GB workspace) and cached. ⚠️ Engines are **GPU-specific** and must be rebuilt when the GPU changes.

⚠️ **Engine files are also TensorRT-version-locked**: an `.engine` built with one TensorRT release fails to deserialize with another (the AI pipeline then falls back to "preview-only"). For this reason the Dockerfile **pins TensorRT to 11.3.0** (`ARG TENSORRT_VERSION`) — do not let apt resolve a newer version silently.

**Building engines inside Docker** (e.g. after deleting the `.engine` files) additionally requires the per-architecture builder resource library. The runtime image ships `libnvinfer_builder_resource_sm75.so` (GeForce MX450/Turing) **and** the PTX fallback; on a sm75 GPU TensorRT does **not** fall back to PTX — a missing sm75 resource makes `createInferBuilder` fail. The first engine build after deletion can take several minutes (ResNet-50 tactic timing on a 2 GB GPU).

### 4.5 Notable features

- **Camera auto-discovery**: local V4L2 devices + ONVIF WS-Discovery + optional deep RTSP subnet scan with credential probing.
- **Quality gates**: motion short-circuit, blur, side-profile, and far-distance filters skip recognition on unusable frames.
- **Unknown-face review**: unrecognized faces are persisted (crop JPEG + embedding) for later enroll-as-new or merge-with-existing via API.
- **Attendance engine**: work sessions, breaks, tardiness vs. schedules & grace minutes, location logging, daily summaries.
- **Hot reconfiguration**: cameras/modules can be started, stopped, and reconfigured at runtime without a restart.
- **License gating**: `core/config/license.json` controls allowed modules and max cameras (currently a development placeholder).
- **Graceful shutdown** on SIGINT/SIGTERM.

---

## 5. Frontend (Flutter)

### 5.1 Two packages under `frontend/flutter/`

| Package | Role |
|---|---|
| `attendance_console` | The Flutter **web** admin console — feature-first MVVM (Riverpod 3), go_router, Material 3, fa (Jalali/RTL) + en |

> Web-only by design: the console talks to the backend exclusively over the REST API. A previous multi-platform app and several legacy frontend experiments have been retired and removed from the repository.

### 5.2 App structure (`attendance_console/lib/`)

```
lib/
├── main.dart               # boots settings, sets up clean URLs
├── app.dart                # MaterialApp.router: themes, locales fa/en, router
├── core/
│   ├── app_theme.dart      # M3 light/dark themes, motion tokens
│   ├── app_strings.dart    # fa/en string table
│   ├── formatters.dart     # Jalali/Gregorian dates, Persian digits, durations
│   └── router.dart         # go_router — StatefulShellRoute (URL per page)
├── data/
│   ├── api_client.dart     # REST client (+ live-preview plumbing)
│   ├── models.dart         # typed models over the API JSON
│   └── repositories.dart   # persons / cameras / zones / attendance / unknown
├── features/               # dashboard, live, people (+person detail),
│                           # attendance, cameras, zones, unknown, reports, settings
├── shared/widgets.dart     # panels, stat cards, chips, async gates, avatars…
└── providers.dart          # Riverpod providers (data cache + invalidation)
```

### 5.3 Pages (URL-driven sidebar navigation)

| # | Page | Route | Purpose |
|---|---|---|---|
| 0 | Dashboard | `/` | KPIs, 7-day attendance chart, camera status, today board (30 s auto-refresh) |
| 1 | Live | `/live` | Camera wall — subscribe → polled JPEG previews → unsubscribe, fullscreen view |
| 2 | People | `/people` | Directory, search, person CRUD, access level, photo upload |
| 3 | Person profile | `/people/:id` | 24 h day timeline + breaks, schedule editor, last seen (deep-linkable) |
| 4 | Attendance | `/attendance` | Daily board with status chips, per-date navigation (Jalali/Gregorian) |
| 5 | Cameras | `/cameras` | Discovery scan (incl. deep network scan), manual add/edit, connection test, stream/AI toggles |
| 6 | Zones | `/zones` | Zones with required access level |
| 7 | Unknown faces | `/unknown` | Review queue: enroll as new / merge with existing / discard |
| 8 | Reports | `/reports` | Weekly work-hours per person, team tardiness |
| 9 | Settings | `/settings` | Server URL, theme (light/dark/system), language |

Navigation is URL-based (`go_router` + `StatefulShellRoute`) — every page is
deep-linkable and each branch preserves its own state. The shell is an
adaptive sidebar (256 px → icon rail → drawer). RTL-aware layout for Persian;
all state swaps animate (implicit Material 3 motion).

### 5.4 Platform support

| Platform | Status |
|---|---|
| **Web** | ✅ The only supported frontend platform (Docker + nginx, port 3000) |

> Desktop/mobile native frontends are intentionally not part of this repository;
> the backend's C FFI (`libface_backend.so` / `face_backend.dll`) remains
> available for embedding the engine in-process from other applications.

---

## 6. Face Recognition Pipeline

Per camera, per frame:

1. **Capture** — `CameraStreamer` grabs frames (RTSP/V4L2) into a circular buffer.
2. **Motion short-circuit** — frames without motion skip the heavy stages.
3. **Detection** — TensorRT face detection + head detection (head detector improves recall for hard poses).
4. **Tracking** — ByteTrack assigns stable `track_id`s across frames.
5. **Quality gates** — blur, side-profile, and distance filters reject unusable faces.
6. **Embedding** — the best crop per track is aligned and embedded into a 512-d L2-normalized vector (async worker queue).
7. **Matching** — `FaceSearcher` runs FAISS inner-product search against the `reference_faces` gallery (configurable threshold, typically 0.3–0.6).
8. **Action** —
   - Matched → camera type drives attendance (`ENTRY` → arrival, `EXIT` → departure, `BOTH` → direction heuristics) + zone access-level check.
   - Unmatched → crop + embedding saved to `unknown_faces` for human review.
9. **Preview** — annotated JPEG is published to `CameraPreviewHub` and polled by the UI (`GET /api/preview/{uid}/jpeg`, ~15 fps).

---

## 7. REST API Reference

Base URL: `http://<host>:8080` · All responses JSON · CORS `*` · **No authentication** (intended for trusted LAN / nginx front).

| Area | Method & Path | Purpose |
|---|---|---|
| **Health** | `GET /api/health` | Status + version |
| **Persons** | `GET /api/persons` · `GET /api/persons/{id}` | List / get person |
| | `POST /api/persons` | Create `{name, code}` |
| | `PUT /api/persons/{id}` | Update · `DELETE /api/persons/{id}` (rebuilds FAISS index) |
| | `PUT /api/persons/{id}/access-level` | Set access level |
| | `GET|PUT /api/persons/{id}/picture` | Fetch / upload profile photo (base64) |
| **Cameras** | `GET /api/cameras` · `POST /api/cameras` · `GET|DELETE /api/cameras/{uid}` | CRUD |
| | `POST /api/cameras/discover` | V4L2 + ONVIF WS-Discovery (+ `{"deep":true}` RTSP scan) |
| | `POST /api/cameras/test` | RTSP connectivity/credential probe |
| | `PUT /api/cameras/{uid}/active` | Toggle streaming |
| | `PUT /api/cameras/{uid}/configure` | Full config incl. per-module overrides |
| | `GET|PUT /api/cameras/{uid}/module/{module}` | Per-camera module config |
| **Zones** | `GET /api/zones` · `POST /api/zones` · `GET /api/zones/{id}` · `PUT /api/zones/{id}/access-level` | Zone CRUD + access level |
| **Attendance** | `GET /api/attendance/summary?personId=&date=` | Daily summary |
| | `GET /api/attendance/timeline?personId=&date=` | Break records |
| | `GET /api/attendance/tardiness?date=` | Tardiness list |
| | `GET /api/attendance/current-location/{personId}` | Last seen zone/camera |
| **Modules** | `GET /api/modules` | Pipeline modules + defaults |
| **Settings** | `GET|PUT /api/settings/{key}` | Key/value settings |
| **Schedules** | `PUT /api/schedules` · `GET /api/schedules/{personId}` | Work schedule upsert/get |
| **Preview** | `GET /api/preview/{uid}/jpeg` | Latest annotated frame (headers: `X-Width`, `X-Height`, `X-Fps`) |
| | `GET /api/preview/{uid}/subscribe` · `/unsubscribe` | Ref-counted preview subscription |
| **Unknown faces** | `GET /api/unknown-faces` · `GET /api/unknown-faces/{id}/image` | List / serve crop |
| | `POST /api/unknown-faces/{id}/enroll` | Enroll as new person |
| | `POST /api/unknown-faces/{id}/merge` | Merge into existing person |
| | `DELETE /api/unknown-faces/{id}` | Discard |

---

## 8. Database

**SQLite** (`backend/attendance.db`), accessed through `services/storage/FaceDatabase` (raw sqlite3 C API; schema auto-created with inline column migrations).

**Tables (14):** `persons`, `reference_faces` (512-float BLOB embeddings), `unknown_faces`, `attendance_logs`, `raw_detections`, `system_settings`, `person_schedules`, `zones`, `cameras`, `camera_module_config`, `work_sessions`, `breaks`, `location_log`, `tardiness_log`.

**Manual SQL migrations** live in `backend/migrations/`:
- `20260512_add_module_config_and_access_levels.sql`
- `20260526_add_stream_enabled_to_cameras.sql`

---

## 9. Installation & Setup

### 9.1 Prerequisites

| Component | Requirement |
|---|---|
| OS | Linux (primary) or Windows (backend DLL build) |
| GPU | NVIDIA GPU with recent driver |
| CUDA | Toolkit **12.6.3** + cuDNN |
| TensorRT | `libnvinfer`, `nvonnxparsers`, plugin libs (apt or NVIDIA repo) |
| Build tools | CMake ≥ 4.0, Conan 2, GCC/Clang with C++20, Python 3 (for Conan venv) |
| Git LFS | `git lfs install` once — model weights (`.onnx`, ~187 MB) are stored via Git LFS and are fetched automatically on clone |
| GStreamer | `libgstreamer1.0-dev` + base plugins |
| Flutter SDK | ≥ 3.44 (Dart ≥ 3.12) — only for the frontend |
| Docker | Engine + **nvidia-container-toolkit** (for the containerized deployment) |
| Models | ONNX weights ship in the repo via **Git LFS** (`backend/core/models/*.onnx`). TensorRT `.engine` files are *not* stored — they are built automatically on first run for the local GPU/TensorRT |

### 9.2 Option A — Docker Compose (recommended)

The simplest full-stack path (GPU passthrough included):

```bash
# Prerequisites: Docker, nvidia-container-toolkit, Flutter SDK (for web build)
make up          # Builds Flutter web locally, then docker compose up --build
# or step by step:
./scripts/build-frontend.sh   # flutter build web (strips FFI deps)
docker compose up --build
```

- **Frontend**: http://localhost:3000 (nginx → proxies `/api/` to backend)
- **Backend**: http://localhost:8080
- Persisted volumes: `backend-data` (crops/data); host-mounted `backend/attendance.db` and `backend/core/models`.

#### Camera discovery in Docker (networking)

The backend container runs with **`network_mode: host`** — this is *required* for
finding cameras on the local network. Docker's default bridge network:

1. does not forward **multicast**, so ONVIF **WS-Discovery** probes (`239.255.255.250:3702`)
   never leave the container — no ONVIF camera ever sees them, and
2. hides the real LAN from the container — the deep RTSP/ONVIF subnet scan would
   only see Docker's internal `172.x` subnets, never `192.168.x.x`.

With host networking the container behaves like a native app: multicast goes out
through the physical interface, and the subnet scan covers the real LAN (and VPN
interfaces, including networks wider than /24).

Consequences of `network_mode: host`:
- The backend listens directly on the host — the same `http://<host>:8080` as before
  (no `ports:` mapping needed for it).
- The frontend nginx container reaches it via `host.docker.internal`
  (mapped with `extra_hosts: host-gateway` in `docker-compose.yml`), not via the
  Docker service name.

If you cannot use host networking (e.g. rootless Docker), keep the default bridge
and set `CAMERA_SCAN_SUBNETS` on the backend service instead, e.g.:

```yaml
  backend:
    environment:
      - CAMERA_SCAN_SUBNETS=192.168.1.0/24,10.142.76.0/22
```

The deep TCP scan (RTSP/ONVIF/HTTP probing) then targets those subnets — note that
multicast WS-Discovery is still impossible on a bridge network, so only the deep
scan will find cameras in that mode.

Useful commands (also documented in `docker-compose.yml` header):

```bash
docker compose down              # Stop & remove
docker compose down -v           # Stop & delete volumes
docker compose logs -f backend   # Follow backend logs
docker compose logs -f frontend  # Follow frontend logs
```

### 9.3 Option B — Build backend natively (Linux)

```bash
cd backend
./build.sh
# → exports local Conan recipes (conan-custom/), conan install,
#   then cmake --preset conan-release && cmake --build
./build/admin_server            # REST server on :8080
./build/FaceAuthentication      # Native demo with GUI windows
```

Windows backend (shared library only):

```powershell
cd backend
./build_backend.ps1             # Ninja build → face_backend.dll
```

### 9.4 Option C — Web console development (against a running backend)

```bash
cd frontend/flutter/attendance_console
flutter pub get
flutter run -d chrome
# In debug the console defaults to http://localhost:8080; point the backend
# out in Settings → Server URL (e.g. http://localhost:8080) and save.
```

---

## 10. Project Structure

```
FaceAuthentication/
├── backend/                        # C++20 / CUDA engine + REST server
│   ├── main.cpp                    # Demo entry (OpenCV GUI)
│   ├── main_admin_server.cpp       # REST server entry (production)
│   ├── CMakeLists.txt              # Targets: face_backend, FaceAuthentication, admin_server
│   ├── conanfile.txt               # Dependency manifest (OpenCV, FAISS, SQLite…)
│   ├── build.sh / build_backend.ps1
│   ├── Dockerfile                  # 2-stage CUDA build → admin_server image
│   ├── http/                       # http_server.cpp (41 routes) + vendored httplib.h
│   ├── ffi/                        # face_ffi.h/.cpp — C ABI for Flutter/desktop embedding
│   ├── core/
│   │   ├── config/                 # Config.h/.cpp, config.json, license.json
│   │   ├── license/                # LicenseManager (module/camera entitlements)
│   │   ├── models/                 # ONNX + TensorRT engines (det/head/recognition)
│   │   ├── EventBus.h, PluginRegistry.h, IModule.h, SFace.h
│   │   └── crypto/ (Md5, Sha1 — license hashing)
│   ├── modules/
│   │   ├── pipeline/               # FacePipeline — orchestrates detect→track→gates→embed
│   │   ├── detection/              # CDetection (faces), head/CHead (heads)
│   │   ├── tracking/               # CTrack (ByteTrack) + quality gates (CFar, CMotion, CProfile)
│   │   ├── recognition/            # CRecognition, FaceSearcher (FAISS), FaceAuthenticator
│   │   ├── attendance/             # AttendanceManager, WorkSessionTracker
│   │   ├── camera/                 # CameraManager, CameraDiscovery, CameraStreamer, ONVIF client/PTZ
│   │   └── draw/                   # Detection/Recognition overlay drawers
│   ├── services/
│   │   ├── camera/CameraOrchestrator.*   # ★ Main runtime (per-camera threads, matching, persistence)
│   │   ├── stream/CameraPreviewHub.*     # JPEG preview pub/sub
│   │   ├── storage/FaceDatabase.*        # SQLite layer (14 tables)
│   │   └── buffer/                       # Thread-safe circular buffer
│   ├── migrations/                 # Manual SQL migrations
│   ├── conan-custom/               # Local Conan recipes (byte-track, tensorrt-runtime, log libs)
│   └── attendance.db               # Live SQLite database
│
├── frontend/
│   └── flutter/
│       └── attendance_console/     # ★ Flutter web console (REST-only)
│           ├── lib/                # core/ data/ features/ shared/ providers
│           ├── web/ + assets/fonts # Vazirmatn typeface
│           ├── Dockerfile + nginx.conf               # web deployment
│           └── test/               # widget tests (all pages via real router)
│
├── scripts/build-frontend.sh       # builds the console for deployment
├── docker-compose.yml              # backend (GPU) + frontend (nginx)
├── Makefile                        # make up / build / down
└── PROJECT_COMPLETE_ANALYSIS.md    # Deep-dive analysis document
```

---

## 11. Platforms & Deployment

| Deployment | How | Frontend ↔ Backend |
|---|---|---|
| **Docker (primary)** | `make up` → GPU backend + nginx web frontend | REST via `/api/` proxy |
| **Windows desktop** | CI-built zip (`attendance-app-windows`) with `face_backend.dll` | In-process FFI |
| **Linux desktop** | `flutter run -d linux` + `libface_backend.so` | In-process FFI |
| **Android / iOS** | Scaffolded but not production-ready | Would use FFI (bridge supports it) |

---

## 12. Configuration

### Backend — `backend/core/config/config.json` (parsed by `FaceConfig`)

| Section | Contents |
|---|---|
| `models` | Paths to detection / head / recognition ONNX + engine files |
| `thresholds` | Detection 0.4, head 0.6, IoU 0.4, distance limits |
| `tracker` | ByteTrack params (buffer 30, thresholds 0.5/0.6/0.8) |
| `motion` | Motion short-circuit params |
| `pipeline` | `embedding_queue_max_size: 128` |
| `database` | `path: "attendance.db"`, `crops_dir: "data/crops"` |
| `recognition` | `faiss_threshold`, motion window |
| `attendance` | Default arrival 08:00 / departure 17:00, grace 15 min |
| `modules` | Enable + params for detection, tracking, recognition, quality_gate, liveness (disabled placeholder), attendance |

- **License**: `core/config/license.json` (modules, max_cameras, expiry, signature — dev placeholder).
- **Per-camera overrides**: stored in the `camera_module_config` DB table.
- **CLI**: `admin_server [configPath] [dbPath] [port]`.
- No environment variables are read by the C++ code; paths are CWD-relative.

### Frontend — `--dart-define` + Settings screen

| Define | Default | Purpose |
|---|---|---|
| `FACE_BACKEND_LIBRARY` | `libface_backend.so` | FFI library path (native only) |
| `FACE_CONFIG_PATH` | `core/config/config.json` | Backend config passed to in-process engine |
| `FACE_DB_PATH` | `attendance.db` | SQLite path for in-process engine |

Backend host/port and locale are persisted at runtime via `shared_preferences`.

---

## 13. Testing & CI

- **Backend**: no test suite (no GTest/Catch2). The closest functional check is `POST /api/cameras/test`.
- **Console**: widget tests under `frontend/flutter/attendance_console/test/` — boot the real app (router + shell) with fake repositories and render every page; `flutter analyze` is clean (unused code promoted to errors).
- **CI**: none at the moment (the previous Windows desktop workflow shipped with the retired app and was removed).

---

## 14. Security Notes

> ⚠️ Points to address before any production/networked deployment:

1. **No API authentication** — the REST API is completely open (no JWT/sessions/keys). Intended for a trusted LAN or behind a reverse proxy.
2. **Camera credentials in plaintext** — RTSP usernames/passwords are stored in SQLite and returned by `GET /api/cameras`.
3. **CORS `*`** on every route.
4. **License signature** is a development placeholder.

---

*Generated for the FaceAuthentication project. See also: `PROJECT_COMPLETE_ANALYSIS.md` (deep-dive analysis) and `backend/AGENT_BRIEF_HeadTrack_Pipeline.md` (pipeline design doc, Persian).*

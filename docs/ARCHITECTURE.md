# Architecture

This document explains how the pieces fit together and, more importantly, *why* they are split the
way they are. Everything below is derived from the sources in this repository; where something is
aspirational it says so.

---

## 1. Layers

```
 transport     http/  (cpp-httplib: REST + MJPEG)
                    │  calls only through ↓  (the C ABI is the seam)
 abi           ffi/face_ffi.{h,cpp}      72 extern "C" entry points
                    │
 services      services/  orchestration, lifecycle, storage, buffers, streaming
                    │
 modules       modules/  camera · detection · tracking · recognition · attendance · draw · pipeline
                    │
 core          core/  config, module registry, entitlements, xml, crypto, shared result types
                    │
 native        TensorRT · OpenCV · FAISS · SQLite · GStreamer · Eigen/uuid (via Conan)
```

Rules the code follows:

- `modules/*` never include anything from `http/`; the transport layer depends on the pipeline,
  never the reverse.
- `http/http_server.cpp` only calls `face_*` functions. That keeps one obvious place to look for
  "what can an external host do?" and makes the FFI surface the de-facto integration contract.
- `core/` is dependency-light and shared by everything: configuration, result types (`SFace.h`),
  the module registry, the entitlement file and small utilities.

## 2. Runtime topology

```
                     ┌──────────────────────────────────────────────┐
  camera thread A ──▶│ CameraPreviewHub  (latest JPEG/BGR per camera)│──▶ HTTP MJPEG responder
  camera thread B ──▶│      ▲ bounded safe_circular_buffer<cv::Mat>  │──▶ HTTP MJPEG responder
                     └──────┼────────────────────────────────────────┘
                            │ publish
  CameraOrchestrator ───────┴── one worker thread per camera UID
            │
            ▼  frame
        FacePipeline ──▶ CDetection (TensorRT) ──▶ CHead (TensorRT) ──▶ associate by IoU
            │                          └── motion gate: skip inference if the region is static
            ├──▶ CTrack / CFar / CMotion (ByteTrack: Kalman + Hungarian)
            ├──▶ CRecognition → FaceSearcher (FAISS IndexFlatIP) → FaceAuthenticator (thresholds)
            └──▶ AttendanceManager → WorkSessionTracker → FaceDatabase (SQLite) + crops/
```

Thread ownership, which is where a design like this usually breaks:

| Structure | Owner | Synchronisation |
|-----------|-------|-----------------|
| `cv::VideoCapture` / GStreamer pipeline | the camera worker thread created by `CameraOrchestrator` | never touched from another thread; reconfiguration goes through the orchestrator, which restarts the worker |
| frame buffer (`safe_circular_buffer<cv::Mat>`) | producer = camera worker, consumers = hub readers | mutex-guarded push/front/back/pop; bounded, so a stalled consumer drops frames instead of growing memory |
| `CameraPreviewHub` latest-frame cache | hub | per-camera mutex, readers get a copy |
| FAISS index | process-wide, mutated only on enrol/merge/delete | mutation is followed by `rebuildIndex()` through the orchestrator so workers pick up the new index |
| `sqlite3*` handle | opened per FFI/HTTP call (`face_db_open` / `face_db_close`) | Each handle is used from one thread only; SQLite's default serialised threading mode covers accidental sharing. No `busy_timeout` or `foreign_keys` pragma is configured — see [SCHEMA.md](SCHEMA.md#notable-schema-properties). |

The **preview hub** exists for one reason: capture, inference and serving have completely different
pace and failure modes. Decoupling them through a bounded cache means a browser tab that stops
reading MJPEG cannot slow a camera thread, and a saturated GPU cannot blank the live view.

## 3. Per-frame path

For each frame delivered to `FacePipeline` (see `modules/pipeline/FacePipeline.cpp`):

1. **Motion gate** (`modules/tracking/CMotion.cpp`): background subtraction with configurable blur
   and dilate kernels; if the changed area is below `motion.area_threshold`, expensive stages are
   skipped. This is the single biggest throughput lever for a corridor camera that is mostly empty.
2. **Face detection** (`modules/detection/CDetection.cpp`): letterbox to the model input, normalise
   (`-127.5/128` scale), run the TensorRT engine, decode multi-scale keypoint outputs, apply
   `thresholds.detection`.
3. **Head detection** (`modules/detection/head/CHead.cpp`) and **face↔head association** by IoU
   (`thresholds.head_face_iou`): heads without faces are tracked as "present but not recognisable",
   which keeps IDs stable for someone walking away from the camera.
4. **Association across frames** (`modules/tracking/*`): ByteTrack with a Kalman filter and
   Hungarian assignment; `tracker.track_buffer` controls how many frames an ID survives unseen.
5. **Quality gate** (`modules/*` + `config.modules.quality_gate`): blur (Laplacian variance),
   side-profile and far-distance gates. Rejected crops never reach recognition or storage.
6. **Embedding + search** (`modules/recognition/`): the aligned crop becomes a 512-float
   embedding, which is searched in the FAISS index; `recognition.faiss_threshold` decides
   *known* vs *unknown*. Unknown faces go to the review queue with their crop, not to the DB as a
   person.
7. **Attendance** (`modules/attendance/`): a stable track + person id produces an arrival; leaving
   the field of view produces a departure; `WorkSessionTracker` folds those into sessions, breaks,
   tardiness and daily summaries.

Steps 2, 3, 6 and 7 are individually toggleable per camera through `camera_module_config` and the
`/api/cameras/{id}/modules` endpoints, so a zone can run detection+tracking only (counting) or the
full recognition chain (access control).

## 4. Module registry and entitlements

`core/PluginRegistry.{h,cpp}` keeps a name → factory map. `core/StaticModule.{h,cpp}` is the base
for the built-in modules (detection, tracking, recognition, quality gate, liveness, attendance).
`LicenseManager` reads `core/config/license.json` (modules, `max_cameras`, `expires`) and the
registry refuses to load modules that are not in the entitlement set.

Two honest caveats, both repeated in [SECURITY.md](../SECURITY.md):

- the `signature` field of the license file is parsed but **not verified**, so this is a
  configuration/feature-flag mechanism, not an anti-tamper boundary;
- `max_cameras` is enforced where the orchestrator starts workers, not at every call site, so it is
  advisory under unusual API sequences.

If you want to add a feature: implement `IModule`, register a factory in the registry, add the
module name to the license example and to `config.example.json` under `modules`, and expose it via
the per-camera module config endpoints. That is the same path the built-in modules take.

## 5. Boundary with the outside world

| Boundary | Mechanism | Notes |
|----------|-----------|-------|
| C++ ↔ anything else | `extern "C"` handles + JSON strings, see `docs/FFI.md` | No C++ types, no exceptions, caller-frees memory. |
| HTTP ↔ pipeline | same C functions | Deliberate: the API cannot reach anything the library does not expose. |
| app ↔ cameras | V4L2, RTSP, ONVIF SOAP/XML | `modules/camera/`; ONVIF auth via WS-Security `PasswordDigest` (`core/crypto/Sha1`). |
| app ↔ disk | SQLite + `crops/` | `services/storage/FaceDatabase.cpp` creates tables idempotently and patches missing columns in code. The files in `migrations/` are the **manual** (DBA/upgrade) path — nothing in the binary reads that directory; see [SCHEMA.md](SCHEMA.md#migration-policy). |
| app ↔ GPU | TensorRT engines via the `tensorrt-runtime` recipe | Engines are built per GPU from ONNX; see `docs/MODELS.md`. |

## 6. Where the design would go next

- **Back-pressure with policy.** The frame buffer drops when full; a real deployment wants
  per-camera policies (drop-oldest vs sample-vs-block) and metrics around drops.
- **Async recognition.** Recognition is on the frame path today; a queue with bounded staleness
  would decouple "who is in front of the camera now" from "who was here at 08:03".
- **Idempotent attendance writes.** Arrival/departure dedupe is track-keyed; day rollover and
  dangling-session reconciliation are still `TODO` in `WorkSessionTracker`.
- **Event stream instead of polling.** The dashboard polls `/api/dashboard/stats`; SSE over the
  existing `EventBus` would be a small change with a big UX payoff.
- **Split the storage service.** `FaceDatabase` is ~1,250 lines and covers every table; a per-aggregate
  repository layer would be easier to test. (Not done here because the change is mechanical churn on
  code I could not rebuild on a GPU machine in this environment.)

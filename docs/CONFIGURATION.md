# Configuration

The application reads a single JSON document, by default `core/config/config.json`
(the path is `argv[1]`). The tracked template is
[`core/config/config.example.json`](../core/config/config.example.json); your local copy is
git-ignored so deployment-specific values never end up in the repository.

Loading and defaults live in [`core/config/Config.cpp`](../core/config/Config.cpp): every key has an
in-code default, a missing file or a malformed value makes `FaceConfig::IsValid()` return `false`,
and `Reload()` re-reads the same file at runtime (the HTTP settings endpoints do not write this file —
they write `system_settings` in SQLite, which is a different store).

## `models` — inference artifacts

| Key | Default | Notes |
|-----|---------|-------|
| `models.detection_onnx` / `detection_engine` | `models/det_500m_FP32.*` | Face detector. The engine is preferred when present; the ONNX file is only used for conversion. |
| `models.recognition_onnx` / `recognition_engine` | `models/webface_r50.*` | Embedding model; produces the 512-float vectors. |
| `models.head_onnx` / `head_engine` | `models/headdetection_v12.*` | Head detector used for presence/association. |

Paths are resolved relative to the project root or to `core/config`, which is why `core/models/…`
files are found with the default values. See [MODELS.md](MODELS.md).

## `thresholds` — detection tuning

| Key | Example | Effect |
|-----|---------|--------|
| `thresholds.detection` | `0.4` | Face confidence floor. Lower = more candidates, more false boxes in the queue. |
| `thresholds.head` | `0.6` | Head confidence floor. |
| `thresholds.head_face_iou` | `0.4` | IoU needed to consider a head and a face the same object. |
| `thresholds.far_height` / `far_width` | `0.036` / `0.017` | Normalised box size below which a face is treated as too far away. |

## `tracker` — ByteTrack

| Key | Example | Notes |
|-----|---------|-------|
| `tracker.track_buffer` | `30` | Frames an ID survives without a match. Raise for flickering detection, lower to reduce ID swaps between people. |
| `tracker.track_threshold` / `high_threshold` | `0.5` / `0.6` | Low/high confidence split used by the two-stage ByteTrack association. |
| `tracker.match_threshold` | `0.8` | IoU/similarity required to keep an ID. |

## `motion` — cheap pre-filter

| Key | Example | Notes |
|-----|---------|-------|
| `motion.area_threshold` | `1000` | Changed pixels required before detection runs. |
| `motion.buffer_frames` | `5` | Frames averaged before deciding the scene is static. |
| `motion.diff_threshold` | `20` | Per-pixel difference threshold. |
| `motion.blur_kernel_size` / `dilate_kernel_size` | `5` / `5` | Morphology on the foreground mask; bigger = less noise, more blur-induced misses. |

## `pipeline`

| Key | Example | Notes |
|-----|---------|-------|
| `pipeline.embedding_queue_max_size` | `128` | Bounded queue between the frame path and recognition. When it fills, frames are dropped rather than blocking capture. |

## `database`

| Key | Example | Notes |
|-----|---------|-------|
| `database.path` | `attendance.db` | SQLite file; created (with all tables) on first open. |
| `database.crops_dir` | `crops` | Where face crops are written and what `/api/files/{path}` is confined to. Created on demand; contents are git-ignored. |

## `license`

| Key | Example | Notes |
|-----|---------|-------|
| `license.path` | `config/license.json` | Entitlement file consumed by `core/license/LicenseManager.cpp`; template at [`core/config/license.example.json`](../core/config/license.example.json) with `modules`, `max_cameras`, `expires`. The `signature` field is parsed but **not verified** — treat this as feature configuration, not a security boundary ([SECURITY.md](../SECURITY.md)). |

## `recognition`

| Key | Example | Notes |
|-----|---------|-------|
| `recognition.faiss_threshold` | `0.6` (top-level) / `0.3` (inside `modules.recognition`) | Cosine similarity required to call a match. The per-module value wins for module-driven paths — set both deliberately. Higher = fewer false positives, more people stuck in the unknown queue. |
| `recognition.motion_window_size` | `10` | Frames of displacement history per track. |
| `recognition.motion_threshold_px` | `15.0` | Pixels of movement below which a detection is treated as a static/false positive (e.g. a photo of a face). |

## `attendance`

| Key | Example | Notes |
|-----|---------|-------|
| `attendance.default_arrival` / `default_departure` | `08:00` / `17:00` | Used when a person has no `person_schedules` row. |
| `attendance.late_grace_minutes` | `15` | Grace before an arrival counts as tardy. |

## `modules` — per-feature configuration

Each entry is `{ "enabled": bool, ...params }` and can be overridden per camera through
`camera_module_config` + the `/api/cameras/{id}/modules/{name}` endpoints.

| Module | Extra keys | Notes |
|--------|-----------|-------|
| `detection` | — | TensorRT face/head detection stage. |
| `tracking` | `motion_short_circuit`, `motion_threshold`, `motion_buffer`, `motion_diff_threshold`, `blur_kernel_size`, `dilate_kernel_size` | When `motion_short_circuit` is `true` the module repeats the motion gate at the tracking stage; useful for cameras that run tracking without recognition. |
| `recognition` | `faiss_threshold`, `motion_window_size`, `motion_threshold_px` | Per-module overrides of the values above. |
| `quality_gate` | `enabled`, `use_blur_gate`, `blur_threshold`, `use_side_profile_gate`, `use_far_distance_gate` | Rejects crops before enrolment/search. `blur_threshold` is a Laplacian-variance value: raise it on noisy streams, lower it on crisp ones. **Gotcha:** `far_height_threshold` / `far_width_threshold` also appear in the template but are *not* read here — the effective values are `thresholds.far_height` / `thresholds.far_width`. They are kept in the example for documentation of intent only, and removing them (or wiring them up in `Config.cpp`) is a good first cleanup. |
| `liveness` | — | Present and disabled by default; the hook exists in the pipeline but no model is wired. |
| `attendance` | — | Event recording into `attendance_logs` / `work_sessions`. |

A module absent from the license file cannot be loaded at all, regardless of `enabled`.

## What is *not* configurable via JSON

| Thing | Where instead |
|-------|---------------|
| HTTP port (`8080`), crops dir used by the server, DB path for the admin server | `HttpServerConfig` in [`http/http_server.h`](../http/http_server.h), set from `main*.cpp`. The port is intentionally a code constant here; changing it is a one-line patch (or a `--port` flag, which is a good first PR). |
| Runtime settings that the dashboard edits | `system_settings` table via `GET/PUT /api/settings/{key}`. |
| Per-camera capture profile (width/height/fps/encoding) | camera row + `camera_module_config` through the API. |
| Log provider name used by `log-factory` | `log_provider_` default `face` in `core/config/Config.h`. |

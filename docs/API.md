# REST API reference

Base URL `http://<host>:8080`. Implemented in [`http/http_server.cpp`](../http/http_server.cpp)
on top of the C FFI ([`ffi/face_ffi.h`](../ffi/face_ffi.h)) — every handler below corresponds to
`face_*` calls, so anything in this document is also reachable from the library directly.

> ⚠️ **No authentication.** See [SECURITY.md](../SECURITY.md). Bind to a trusted network or put an
> authenticating reverse proxy in front of it. Do not port-forward this port.

## Conventions

| Item | Behaviour |
|------|-----------|
| Content type | `application/json; charset=utf-8` for both directions, except MJPEG streams (`multipart/x-mixed-replace; boundary=frame`) and `GET /api/files/*` (`image/*`). |
| Identifiers | Numeric ids in `/api/{resource}/{id}` are matched with the regex `(\d+)`; camera endpoints also accept a string UID form `/api/cameras/by-uid/{uid}`. |
| Errors | `{"error": "<message>"}` where a message is available; several handlers return a bare status with no body (noted below). Status codes used: `200`, `201`, `204`, `400`, `403`, `404`, `500`. |
| Body parsing | JSON bodies are parsed with nlohmann/json; a malformed body throws inside the handler and cpp-httplib converts it into a `500` (see [SECURITY_AUDIT.md](SECURITY_AUDIT.md#3-http-layer)). |
| CORS | `Access-Control-Allow-Origin: *`, `Access-Control-Allow-Headers: Content-Type, Authorization`, and `OPTIONS` pre-flight handling on `/api/*`. |
| Timestamps | Unix epoch **seconds**, integers, unless a field name says otherwise (`date` is `YYYY-MM-DD`). |
| Embeddings | `float32[512]`, L2-normalised before comparison; index is `IndexFlatIP` (cosine). |

Camera credentials are **write-only**: `password` may be supplied on create/update, but reads return
`hasPassword: true|false` and never echo the secret.

---

## Lifecycle

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/health` | Liveness probe. → `{"status":"ok"}` |
| POST | `/api/init` | Initialise the backend (preview hub + orchestrator). Body (optional): `{"config": "<path>", "db": "<path>"}`. → `{"success":true}` or `{"success":false,"error":"..."}` |
| POST | `/api/cleanup` | Release the backend. → `{"success":true}` |
| GET | `/api/modules` | Registered feature modules and their enabled state. `204` if the registry is empty. |

`faceauth` initialises on startup; `faceauth-admin` expects `POST /api/init` (it calls the same FFI
init itself, so this endpoint is for hosts that manage the lifecycle explicitly).

## People

| Method | Path | Body | Notes |
|--------|------|------|-------|
| GET | `/api/persons` | — | Array of persons: `id`, `name`, `code`, `createdAt`, `isKnown`, `picturePath`, `accessLevel`. `500` if the DB cannot be opened. |
| POST | `/api/persons` | `{"name": str, "code": str}` | `201 {"id": <rowid>}`; `400 {"error":"Failed to add person"}` when the insert fails (e.g. duplicate `code`, which is `UNIQUE`); `500` when the DB cannot be opened. There is **no validation of `name`** — an absent name becomes `""` and is accepted, which is a gap rather than a feature ([SECURITY_AUDIT.md](SECURITY_AUDIT.md#4-api-surface-findings)). |
| GET | `/api/persons/{id}` | — | Single person. `404` when unknown (empty body), `500` on DB failure. |
| PUT | `/api/persons/{id}` | `{"name": str, "code": str}` | Update. |
| DELETE | `/api/persons/{id}` | — | Deletes the person, their reference embeddings and attendance rows; rebuilds the index. |
| PUT | `/api/persons/{id}/access-level` | `{"accessLevel": int}` | `1`=restricted … `3`=staff-level gate used by zone checks. |
| GET | `/api/persons/{id}/schedule` | — | `person_schedules` row, `404` if none. |
| PUT | `/api/persons/{id}/schedule` | `{"expectedArrival": "HH:MM", "expectedDeparture": "HH:MM", "graceMinutes": int, "workDays": "1,2,3,4,5"}` | Upsert. |
| POST | `/api/persons/{id}/face` | **raw image bytes** (JPEG/PNG) as the whole body | Runs detection+embedding on the payload, keeps the first detection's 512-float embedding as a reference vector, then rebuilds the FAISS index. `400` when the body is empty, no face was found, or the embedding was rejected. `200 {"success":true}` otherwise. |
| GET | `/api/groups` | — | Distinct `code` prefixes / grouping values from `system_settings`. |
| POST | `/api/groups` | `{"name": str, "value": str}` | `201` on creation. |
| GET | `/api/embeddings/references` | — | All reference embeddings (person id ↔ vector). Large payload: intended for index inspection and rebuild tooling. |

## Unknown-face review queue

Detections that pass the quality gates but score below `recognition.faiss_threshold` are stored in
`unknown_faces` (with a crop under `crops/`) for a human to decide on.

| Method | Path | Body | Notes |
|--------|------|------|-------|
| GET | `/api/unknown-faces` | — | Array of `{id, embedding[], trackId, cameraUid, timestamp, imagePath}`. Note the payload includes full embeddings — see [known limitations](../README.md#known-limitations). |
| POST | `/api/unknown-faces/{id}/enroll` | `{"name": str, "code": str, "accessLevel": int}` | Creates a person from the queued embedding, links it, deletes the queue row, rebuilds the index. `201`. |
| POST | `/api/unknown-faces/{id}/merge` | `{"personId": int}` | Attaches the embedding to an existing person. |
| DELETE | `/api/unknown-faces/{id}` | — | Discard the queued face. |
| GET | `/api/events` | `?limit=` (parsed but **currently ignored**) | Raw `raw_detections` rows (`trackId`, `personId`, `status`, `confidence`, `imagePath`, `timestamp`) for audit/replay. `[]` when there are none. The `limit` parameter is read and then discarded (`(void)limit`), so the response is the whole table — see [SECURITY_AUDIT.md](SECURITY_AUDIT.md#4-api-surface-findings). |
| GET | `/api/files/{path}` | — | Serves a crop from the configured `crops` directory only. Resolved with `weakly_canonical`; anything escaping the base → `403`, missing → `404`. |

## Cameras

| Method | Path | Body | Notes |
|--------|------|------|-------|
| GET | `/api/cameras` | — | All cameras, credentials redacted (`hasPassword`). |
| GET | `/api/cameras/{id}` · `/api/cameras/by-uid/{uid}` | — | Single camera; `404` if unknown. |
| POST | `/api/cameras` | camera JSON (`uid`, `name`, `zoneId`, `type`, `connectionType`, `streamUri`, `username`, `password`, `encoding`, `onvifXaddrs`, `transportProtocol`, `streamEnabled`, `aiEnabled`) | **Upsert keyed by `uid`** — re-posting the same `uid` with changed fields *is* the update path (there is no `PUT /api/cameras/{id}`). Returns `201 {"id": <rowid>}`; `400` when the row cannot be written. When `streamEnabled` or `aiEnabled` is true the handler also calls `face_preview_subscribe(uid)`, which makes the orchestrator start the worker. |

| DELETE | `/api/cameras/{id}` · `/api/cameras/by-uid/{uid}` | — | Stops the worker, removes the row and its module config. |
| PUT | `/api/cameras/{id}/active` · `/api/cameras/by-uid/{uid}/active` | `{"active": bool}` | Enables/disables **capture** (the stream itself). |
| PUT | `/api/cameras/{id}/ai` · `/api/cameras/by-uid/{uid}/ai` | `{"enabled": bool}` | Enables/disables **inference** while keeping the preview alive — used to pause recognition without losing the live view. |
| GET | `/api/cameras/{id}/stream` · `/api/cameras/by-uid/{uid}/stream` | — | MJPEG (`multipart/x-mixed-replace; boundary=frame`) from `CameraPreviewHub`; serves the latest cached frame while the camera is running. `404` when the hub has no such camera. |
| POST | `/api/cameras/discover` | `{"deep": bool}` | Runs V4L2 + ONVIF WS-Discovery (+ RTSP port/path deep scan when `deep`). Returns candidate devices with media profiles and metadata (manufacturer, model, xaddrs) — never credentials. |
| GET | `/api/cameras/{id}/modules` | — | Per-camera module state + config. |
| GET | `/api/cameras/{id}/modules/{name}` | — | One module's `{enabled, config, defaultConfig}`. |
| PUT | `/api/cameras/{id}/modules/{name}` | `{"enabled": bool, "config": object}` | Persists into `camera_module_config` and notifies the orchestrator so the running worker picks it up. |

## Attendance and zones

| Method | Path | Body / query | Notes |
|--------|------|--------------|-------|
| POST | `/api/attendance/arrival` | `{"trackId": str, "personId": int, "timestamp": int}` | Marked through `AttendanceManager`; duplicate arrivals on the same track are deduplicated. |
| POST | `/api/attendance/departure` | same | Closes the arrival side, updates the work session. |
| GET | `/api/attendance/summary` | `?personId=&date=` | Daily summary: arrival, departure, work seconds, break seconds, tardiness, absence, break intervals. |
| GET | `/api/attendance/timeline` | `?personId=&date=` | Ordered events for the day (useful for debugging a disputed record). |
| GET | `/api/attendance/tardiness` | `?date=` | Per-person lateness/absence list. |
| GET | `/api/attendance/current-location/{personId}` | — | Latest `location_log` entry: zone, camera, timestamp. `404` when untracked. |
| GET | `/api/zones` · `/api/zones/{id}` | — | Zones with `requiredAccessLevel`. |
| POST | `/api/zones` | `{"name": str, "description": str, "requiredAccessLevel": int}` | `201`. |
| PUT | `/api/zones/{id}` | `{"name": str, "description": str}` | Update. |
| PUT | `/api/zones/{id}/access-level` | `{"requiredAccessLevel": int}` | Access rule for the zone. |
| DELETE | `/api/zones/{id}` | — | Remove. |

## Dashboard and settings

| Method | Path | Notes |
|--------|------|-------|
| GET | `/api/dashboard/stats` | Aggregates: total persons, present/absent today, active vs offline devices, unknown faces and detections pending review. Composed from per-person daily summaries, so it is O(persons) — cache it in the UI if the roster is large. |
| GET | `/api/dashboard/activity` | Recent activity feed for the dashboard ticker. |
| GET | `/api/settings` | Known setting keys with current values. |
| GET | `/api/settings-all` | Everything in `system_settings`, including descriptions. |
| GET | `/api/settings/{key}` | One value. |
| PUT | `/api/settings/{key}` | `{"value": str, "description": str?}`. |

---

## Worked examples

List cameras, then pause AI on one of them:

```bash
curl -s localhost:8080/api/cameras | python3 -m json.tool
curl -s -X PUT localhost:8080/api/cameras/1/ai -d '{"enabled":false}'
```

Discover cameras on the LAN, register the first result, and check the live preview:

```bash
curl -s -X POST localhost:8080/api/cameras/discover -d '{"deep":true}' | python3 -m json.tool
curl -s -X POST localhost:8080/api/cameras --data @camera.json
curl -sI localhost:8080/api/cameras/1/stream     # multipart/x-mixed-replace; boundary=frame
```

Enrol a face from a photo and confirm the person is recognised afterwards:

```bash
ID=$(curl -s -X POST localhost:8080/api/persons -d '{"name":"A. Student","code":"STU-042"}' \
     | python3 -c 'import sys,json;print(json.load(sys.stdin)["id"])')
curl -s -X POST "localhost:8080/api/persons/$ID/face" --data-binary @portrait.jpg
curl -s localhost:8080/api/embeddings/references | python3 -c 'import sys,json;d=json.load(sys.stdin);print(len(d),"reference vectors")'
```

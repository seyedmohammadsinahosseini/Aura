# Self-audit: what I found reading my own code

A portfolio repository that only shows the polished parts is not worth much. This is the list I
produced when I reviewed this codebase for publication, with severity, evidence and what I actually
did. Everything marked **fixed** is verifiable in the diff of the release commit; everything marked
**documented** is a real limitation that I did not want to change blindly (no GPU here, so a change to
the inference path could not be tested before shipping).

Legend: 🔴 serious for a real deployment · 🟠 should fix · 🟡 hygiene · ⚪ by design

---

## 1. Secrets and data exposure

| # | Finding | Severity | Status |
|---|---------|----------|--------|
| 1.1 | `core/config/config.json` was tracked. It happened to contain no secrets, but tracking a live config next to `config.example.json` is how credentials get committed the first time someone "just adds the camera password". | 🔴 | **fixed** — untracked, added to `.gitignore`; only `*.example.json` templates are in git. |
| 1.2 | `GET /api/cameras*` (and `face_db_get_camera*`) serialised `face::CameraConfig::password` straight back to clients. | 🔴 | **fixed** — the FFI now emits `hasPassword: bool`; the value stays write-only. Verified by reading the serialiser: `{"hasPassword", !c.password.empty()}` in `ffi/face_ffi.cpp`. Streaming is unaffected because `CameraOrchestrator` reads credentials from the DB through `FaceDatabase::getCameraConfig`, not through JSON. |
| 1.3 | `cameras.username` / `cameras.password` are stored in plaintext in `attendance.db`. | 🔴 | **documented** — needs a key-management decision (OS keyring vs. encrypted column vs. external secret store), which is a design change, not a patch. Stated in [SECURITY.md](../SECURITY.md) and [PRIVACY.md](PRIVACY.md). |
| 1.4 | No hardcoded credentials, LAN addresses or hostnames anywhere in the sources — the discovery code probes standard ONVIF/RTSP ports and uses the operator's subnet, and default-credential guessing was removed. | ⚪ | verified: `grep -rE "(192\.168\.[0-9]+\.[0-9]+:[^@]+@|password\s*=\s*\"[^\"]+)` over `core/ modules/ services/ ffi/ http/` returns nothing but the ONVIF WS-Security *format* strings and `= ""` defaults. |
| 1.5 | `int8_calibration.txt` and the `openvino/*.xml.hash` files are derived from model bring-up, not from customer footage. | ⚪ | verified — they contain TensorRT layer hashes and no image data. |

## 2. Authentication and transport

| # | Finding | Severity | Status |
|---|---------|----------|--------|
| 2.1 | No authentication/authorisation on any of the 56 endpoints; `Access-Control-Allow-Origin: *`. | 🔴 | **documented** — plus a concrete, small path to `Authorization` checking in the existing `set_pre_routing_handler`. Roadmap item 1. |
| 2.2 | No TLS. The server is plain HTTP by design here; `httplib` supports SSL but `CMakeLists.txt` does not link OpenSSL for that purpose. | 🔴 | **documented** — front with a proxy; do not route this off-LAN. |
| 2.3 | `license.json` has a `signature` field that is **never verified**; `LicenseManager` only checks that the JSON parses and that `modules` is non-empty, and expiry is compared with `std::mktime` on a `tm` filled with `parseDate`. Anyone can write their own license file. | 🟠 | **documented** — it is a feature-flag mechanism. Real entitlement needs an asymmetric signature (Ed25519) over a canonical byte range, checked with a public key compiled into the binary. Calling this out matters because "license manager" in a codebase can otherwise look like a security control. |
| 2.4 | `max_cameras` from the license is enforced only where workers start, so a caller that opens streams directly through the FFI bypasses it. | 🟡 | **documented** — same root cause as 2.3. |

## 3. HTTP layer

| # | Finding | Severity | Status |
|---|---------|----------|--------|
| 3.1 | `json::parse(req.body)` is called unguarded in several handlers (`/api/persons`, `/api/attendance/arrival|departure`, `/api/zones`). A malformed body throws; cpp-httplib catches it and returns `500`, so nothing crashes, but the client gets the wrong status and the server logs nothing. | 🟠 | **documented** — the tidy fix is a `parseBody(req, res)` helper returning `std::optional<json>` and `400 {"error":"invalid json"}`; it is mechanical across ~10 handlers and belongs with the auth work so the handler signatures change once. |
| 3.2 | `empty catch (...)` in `POST /api/persons/{id}/face`: if the detection JSON is malformed the enrolment fails silently as a `400` with no reason. | 🟠 | **documented** — should surface `face_last_error()`. |
| 3.3 | `GET /api/events` reads a `limit` query parameter and then discards it (`(void)limit;`), returning the whole `raw_detections` table. | 🟠 | **documented** — an honest `TODO` in the API docs rather than a silently wrong parameter; wiring it needs a paginated FFI function (`face_db_get_detections(db, limit, offset)`), which is a bigger ABI change. |
| 3.4 | `POST /api/persons` accepts an empty `name` (it becomes `""`), so a person with no display name can be created and then appears as a blank row in the review queue. | 🟡 | **documented** — validation should live in `FaceDatabase::addPerson` so every entry point (API, FFI, enrol-from-queue) gets it. |
| 3.5 | Path traversal in `GET /api/files/{path}` is prevented by canonicalising against `config.cropsDir` and rejecting escapes (`403`), with `404` for misses. | ⚪ | verified correct. |
| 3.6 | MJPEG handlers hold a pooled handler thread per connected viewer; default pool is `max(8, hardware_concurrency-1)`. | 🟠 | **documented** — availability issue; explicit `ThreadPool` + a bounded viewer count is the fix. |
| 3.7 | `POST /api/cameras` parses the body twice (once for the flags, once inside the FFI). | 🟡 | **documented** — harmless, wasteful. |

## 4. API surface findings

| # | Finding | Severity | Status |
|---|---------|----------|--------|
| 4.1 | `PUT /api/cameras/{id}` does not exist (updates happen by re-`POST`ing the same `uid`, which is an upsert). Earlier docs implied a REST-ful verb that was never implemented. | 🟡 | **fixed in docs** — [API.md](API.md#cameras) now states the real behaviour. Adding the verb is a small PR. |
| 4.2 | `GET /api/unknown-faces` returns a full 512-float embedding per queued face — tens of KB per row, and it is the review screen a dashboard polls. | 🟠 | **documented** in [Known limitations](../README.md#known-limitations); fix is `{id, imagePath, timestamp, confidence}` + a separate `GET /api/unknown-faces/{id}/embedding`. |
| 4.3 | Error responses are inconsistent: some handlers return `{"error": ...}`, several return a bare status with an empty body. | 🟡 | **documented** per-endpoint in [API.md](API.md). |
| 4.4 | `GET /api/modules` returns `204` (no content) rather than an empty array when nothing is registered, which is a surprising shape for a JSON API. | 🟡 | **documented**. |

## 5. Data-lifecycle findings

| # | Finding | Severity | Status |
|---|---------|----------|--------|
| 5.1 | `FaceDatabase::deletePerson` removes `reference_faces`, `raw_detections`, `attendance_logs`, `person_schedules` — but leaves `work_sessions`, `breaks`, `tardiness_log`, `location_log` and the crop files. For a system holding biometric data, an incomplete delete path is a compliance bug, not a cosmetic one. | 🔴 | **documented** with the operational workaround in [PRIVACY.md](PRIVACY.md#4-what-a-real-deployment-must-add) and `scripts/purge_data.sh --person-id`. The proper fix is one transaction in `deletePerson` (or `ON DELETE CASCADE` once 5.2 is done). |
| 5.2 | `FOREIGN KEY` clauses are declared but never enabled: `PRAGMA foreign_keys` is off by default per SQLite connection, and the app never turns it on. The two files in `migrations/` toggle it around their own DDL only. | 🟠 | **documented** — enabling it globally is one line but changes failure modes for existing deployments (inserts that today succeed would start failing), so it should ship with a data-integrity check first. |
| 5.3 | No indexes besides PK/UNIQUE on tables that only ever grow. | 🟠 | **documented** with the exact `CREATE INDEX` statements in [SCHEMA.md](SCHEMA.md#notable-schema-properties); not shipped unmeasured. |
| 5.4 | Nothing reads `migrations/` at runtime, while the README previously claimed migrations were "applied by the app". | 🟡 | **fixed in docs** — [SCHEMA.md#migration-policy](SCHEMA.md#migration-policy) explains the two mechanisms and their limits. |
| 5.5 | No retention job exists for crops/detections. | 🟠 | **partly fixed** — `scripts/purge_data.sh` provides the SQL with a `--dry-run`; scheduling it stays an operator decision. |

## 6. Concurrency and lifetime

| # | Finding | Severity | Status |
|---|---------|----------|--------|
| 6.1 | `crypto::sha1()` allocated with `new uint8_t[]` and freed at the end of the function; correct on the happy path, exception-unsafe. | 🟡 | **fixed** — `std::vector` (RAII). Its behaviour is now pinned by `tests/unit/test_sha1.cpp` against RFC 3174 vectors and every padding boundary (54/55/56/63/64/65/119/120 bytes). |
| 6.2 | The frame queue's contract was undocumented: `push()` returns `false` when full (i.e. frames are *dropped*, not blocked) — easy to misread as back-pressure. | 🟡 | **fixed** — documented in [ARCHITECTURE.md](ARCHITECTURE.md#2-runtime-topology) and pinned by a producer/consumer test that asserts no item is lost or duplicated. |
| 6.3 | `WorkSessionTracker` does not close dangling sessions (`TODO` at the top of the file), so a session stays open if a person is never seen leaving. | 🟠 | **documented** — the fix needs a policy decision (what is a missed departure?) which is deployment-specific. |
| 6.4 | `CDetection`, `CHead` and `CRecognition` normalise one image at a time (`TODO: change to a batch of images`), so throughput is input-pipeline bound rather than GPU bound. | 🟠 | **documented** — batching needs engine rebuilder work I could not validate without a GPU. |
| 6.5 | A vendored `//TODO: why leak?` sits on a `cudaMalloc` in the `tensorrt-runtime` recipe's `output_tensor.h`: an unfixed GPU memory question in a hot path. | 🟠 | **documented** — inherited from that package; it should become a `cudaFree` in a destructor or a scoped buffer, and needs `compute-sanitizer` to confirm. |
| 6.6 | `safe_circular_buffer` lived under a namespace belonging to the internal organisation that originally funded the work (`veera::`), and the orphaned `test/test_BYTETracker.cpp` in the ByteTrack recipe referenced gtest and boost that the recipe never declares. | 🟡 | **fixed** — namespace is now `backend::`, matching the rest of the core; the uncompilable orphan test was deleted rather than left as a trap (the recipe's `exports_sources` no longer mentions it either). |

## 7. Repository hygiene

| # | Finding | Status |
|---|---------|--------|
| 7.1 | No `LICENSE`, no `SECURITY.md`, no CI, no tests, single `Initial public release` commit, `log-entry` referenced by a version-hash directory (`1.0.0-4-83193b47`) that could not be resolved. | **fixed** — MIT license, this audit, `SECURITY.md`, CI with three required jobs, `tests/`, and clean recipe versions (`1.0.0`) that make `conan create conan/recipes/<name>` work as documented. |
| 7.2 | `.gitignore` ignored `*.png` globally, which would also have silently swallowed documentation images. | **fixed** — negation for `docs/images/**`, and CI (`check_repo_hygiene.py`) asserts that every image referenced by a README/docs page exists, so an ignored asset fails the build instead of showing a broken icon. |
| 7.3 | Two crops directories (`crops/` and `data/crops/`) with two different defaults (`config.json` vs `Config.h`). | **fixed** — single `crops/` default in code and config. |
| 7.4 | `project(FaceAuthentication VERSION 0.0.0)` with a PascalCase binary name on Linux, and `file(GLOB_RECURSE ...)` silently missing new files. | **fixed** — `FaceAuthAttendance 1.0.0`, binaries `faceauth` / `faceauth-admin`, `CONFIGURE_DEPENDS` globs, version printed by `--version`, and an `install()` rule. |
| 7.5 | `main*.cpp` logged through `std::cout` while a logging factory was a declared dependency; `Config.h` exposes a `LogCallback` that the entry points ignore. | **documented** — wiring the two together is worthwhile but touches every module's diagnostics; left as a focused PR. |
| 7.6 | `modules/quality_gate.far_height_threshold` / `far_width_threshold` in the config template are never read; the effective values come from `thresholds.far_*`. | **documented** in [CONFIGURATION.md](CONFIGURATION.md#modules--per-feature-configuration). |

---

## How to reproduce the clean parts

```bash
./scripts/check_repo_hygiene.py            # secrets, ignored-artifact and doc-link checks
cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests
grep -rn "hasPassword" ffi/face_ffi.cpp    # 1.2
git log --oneline -- core/config           # 1.1
```

## If you want to help

The highest-value contributions are, in order: the `parseBody` helper + tests (3.1), the delete-path
transaction (5.1), `PRAGMA foreign_keys=ON` behind an integrity check (5.2), and pagination for the
review queues (4.2). [CONTRIBUTING.md](../CONTRIBUTING.md) has the style and test expectations.

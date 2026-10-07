# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-10-07

Prepared for publication of the repository as a public portfolio release. Everything under
*Changed* and *Removed* below is a repository-level change: the build layout, documentation, tooling
and four small code changes are listed separately so that the functional deltas are impossible to miss.

### Added
- `LICENSE` — the project is now MIT-licensed (it previously had no license at all, which legally
  means "all rights reserved" and blocks reuse).
- `tests/` — dependency-free unit tests with CTest wiring, also buildable standalone:
  - `test_sha1.cpp`: RFC 3174 vectors, every SHA-1 padding boundary (54/55/56/63/64/65/119/120-byte
    inputs), determinism and embedded-NUL handling; asserts the documented "raw 20-byte digest" contract.
  - `test_safe_circular_buffer.cpp`: FIFO order, capacity rejection, wraparound, and a
    producer/consumer thread test that verifies no frame is lost or duplicated.
- `.github/workflows/ci.yml` — unit tests, formatting and repository-hygiene checks on every push/PR,
  plus a manual (`workflow_dispatch`) CUDA build job intended for a self-hosted GPU runner.
- `.github/`: issue templates, pull-request template, Dependabot for Actions.
- `.clang-format`, `.editorconfig`, `.gitattributes` (LF normalisation, `linguist-vendored` for
  third-party code, binary guards) — and the whole first-party tree reformatted to the new style.
- `SECURITY.md` (policy, hardening checklist, supported versions) and `docs/SECURITY_AUDIT.md`
  (a self-review with severity and status per finding, including what I did *not* fix and why).
- `docs/`: `ARCHITECTURE.md`, `API.md` (all 56 endpoints, generated from and checked against the
  route table), `FFI.md` (the 71-function C ABI with memory-ownership rules plus a Python/`ctypes`
  example), `BUILDING.md`, `CONFIGURATION.md`, `MODELS.md`, `SCHEMA.md`, `PRIVACY.md`,
  `SCREENSHOTS.md`.
- `docs/images/`: architecture, pipeline and ERD diagrams generated from the source tree, an
  illustrative detection-overlay rendering, a watermarked dashboard concept mockup, and the two
  scripts that regenerate them.
- `scripts/format.sh`, `scripts/check_repo_hygiene.py` (fails on committed weights/databases/
  credentials/config, on broken relative doc links, and on a missing referenced image),
  `scripts/capture_screenshots.sh`, `scripts/purge_data.sh`.
- `CONTRIBUTING.md`, `CHANGELOG.md`, `CITATION.cff`, `conan/README.md`, `third_party/README.md`.
- CLI polish in both entry points: `--help` and `--version`, and a startup banner that prints the
  project version (`FACEAUTH_VERSION_STRING`, injected by CMake).
- `install()` rules for the library, both binaries and `ffi/face_ffi.h`; `GNUInstallDirs` honoured.
- Documented the two crops-adjacent defaults reconciliation (see *Changed*).

### Changed
- **Security:** camera credentials are no longer echoed by the API. `toJson(face::CameraConfig)` in
  `ffi/face_ffi.cpp` returns `hasPassword: bool` instead of `password`. Reads of
  `GET /api/cameras`, `/api/cameras/{id}` and `/api/cameras/by-uid/{uid}` no longer expose stored
  RTSP/ONVIF passwords; writes still accept `password`. Stream startup is unaffected (credentials are
  read from SQLite by `FaceDatabase::getCameraConfig`, not from JSON).
- `core/config/config.json` is no longer tracked; `core/config/config.example.json` is the template and
  copying it is step 7 of the build guide. Prevents live configuration (and any future credential)
  from landing in history.
- Directory layout: `conan-custom/<package>/<version-hash>/` → `conan/recipes/<package>/`. The old
  directory names encoded internal artifact revisions (`1.0.0-2-71f99ef7`, `1.0.0-1-56934db0`,
  `test-0.1`); recipe and dependency versions are now plain `1.0.0` and each `conanfile.py` declares
  `version`, so `conan create conan/recipes/<name>` works exactly as documented.
- `conan/recipes/byte-track`: headers moved `files/ByteTrack/` → `include/ByteTrack/` with the CMake
  source list, `install(DIRECTORY ...)` and the Conan `package()` copy updated to match.
- `http/httplib.h` moved to `third_party/cpp-httplib/include/httplib.h`, the include directory is added
  to the `face_backend` target, and the file is marked `linguist-vendored` so it no longer dominates the
  repository's language statistics (20,724 vendored lines against 12,881 lines of first-party C++).
- Names: CMake project `FaceAuthentication` → `FaceAuthAttendance`, version `0.0.0` → `1.0.0`,
  binaries `FaceAuthentication`/`admin_server` → `faceauth`/`faceauth-admin`.
- Namespace `veera::safe_circular_buffer` → `backend::safe_circular_buffer`, matching the `backend::`
  namespace used by `core/`. The old name referred to the organisation that originally funded the work;
  it was the last trace of it in the tree.
- `Config.h` default `database.cropsDir` is now `"crops"` (was `"data/crops"` while every shipped config
  said `"crops"`); the duplicate `data/crops/` tree was removed.
- `crypto::sha1()` allocates its padded block with `std::vector` instead of `new uint8_t[]`, and the
  header documents that it returns a raw 20-byte digest (and why SHA-1 is acceptable *only* for the
  ONVIF WS-Security digest).
- `core/models/reorder.py` is now a proper CLI tool (`--help`, optional input/output paths,
  `--order` override, explicit missing-file and missing-dependency errors) instead of a script with
  hard-coded filenames and a broken `#!/bin/env` shebang.
- `git rm` of `test/test_BYTETracker.cpp` from the ByteTrack recipe (see *Removed*) and `GLOB` →
  `GLOB ... CONFIGURE_DEPENDS` so new sources are picked up without touching the build system.
- `README.md` rewritten: badge row, table of contents, honest limitations, a reviewer's two-minute
  tour, API/FFI summaries generated from the route table, privacy and security sections.
- Corrected statements inherited from the previous README: `migrations/*.sql` are **not** applied by the
  application (nothing reads that directory — they are a manual upgrade path alongside the idempotent
  in-code column patches), face enrolment posts raw image bytes rather than a multipart form,
  `PUT /api/cameras/{id}` does not exist (the upsert-by-`uid` `POST` is the update path), the FFI has
  71 functions and the schema 14 tables, and `GET /api/events?limit=` is currently parsed and ignored.

### Removed
- `conan/recipes/byte-track/test/test_BYTETracker.cpp`: an orphan test file that referenced gtest and
  Boost headers the recipe never declares and that no CMake target built. Keeping it suggested a test
  suite that does not exist.

### Fixed
- Two real documentation defects that would have cost a reader time: the recipe paths in the build
  instructions, and the enrolment example (`-F 'image=@f.jpg'` → `--data-binary @f.jpg`).
- `.gitignore`'s global `*.png` / `*.jpg` rules would have silently swallowed documentation images;
  `docs/images/**` is explicitly un-ignored and `check_repo_hygiene.py` now fails if a referenced image
  is missing, so the trap cannot silently reappear.

## [0.0.0] - 2026-08-08

### Added
- Initial snapshot: the C++20 face-authentication and attendance backend (camera discovery/ONVIF/RTSP,
  TensorRT detection, ByteTrack tracking, FAISS recognition, attendance/work-session logic, SQLite
  storage, REST admin API and the C FFI boundary), its Conan recipes, model conversion helpers and SQL
  migrations.

[1.0.0]: https://github.com/seyedmohammadsinahosseini/cpp-face-auth-attendance/releases/tag/v1.0.0

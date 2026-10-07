# Storage schema

Everything is created in [`services/storage/FaceDatabase.cpp`](../services/storage/FaceDatabase.cpp)
with `CREATE TABLE IF NOT EXISTS`, so opening a fresh path gives you a complete, usable database.

## Tables

| Table | Purpose | Key columns |
|-------|---------|-------------|
| `persons` | Enrolled identities | `id`, `name NOT NULL`, `code UNIQUE`, `created_at`, `is_known`, `picture_path`, `access_level` (default 2) |
| `reference_faces` | Enrolment embeddings per person | `id`, `person_id → persons`, `embedding BLOB` (512 × float32), `created_at` |
| `unknown_faces` | Review queue for faces below the match threshold | `id`, `embedding NOT NULL`, `track_id`, `camera_uid`, `timestamp`, `image_path` |
| `raw_detections` | Every detection kept for audit/replay | `id`, `person_id`, `track_id NOT NULL`, `embedding NOT NULL`, `image_path`, `timestamp`, `status`, `confidence REAL` |
| `attendance_logs` | Append-only arrival/departure events | `id`, `person_id`, `track_id`, `event_type`, `timestamp` |
| `work_sessions` | One row per person per day | `id`, `person_id`, `date`, `arrival_time`, `departure_time`, `total_work_seconds`, `total_break_seconds`, `is_complete`, `UNIQUE(person_id, date)` |
| `breaks` | Break intervals inside a session | `id`, `session_id → work_sessions`, `person_id`, `break_start`, `break_end`, `break_seconds` |
| `tardiness_log` | Lateness / absence per day | `id`, `person_id`, `date`, `expected_arrival`, `actual_arrival`, `tardiness_seconds`, `is_absent` |
| `person_schedules` | Expected hours per person | `id`, `person_id UNIQUE`, `expected_arrival '08:00'`, `expected_departure '17:00'`, `grace_minutes 15`, `work_days '1,2,3,4,5'`, `updated_at` |
| `zones` | Physical/logical areas for access control | `id`, `name`, `description`, `created_at`, `required_access_level` |
| `cameras` | Registered devices | `id`, `camera_uid UNIQUE NOT NULL`, `name`, `zone_id → zones`, `type`, `stream_enabled`, `ai_enabled`, `connection_type 'RTSP'`, `username`, `password`, `stream_uri`, `encoding`, `onvif_xaddrs`, `transport_protocol 'tcp'`, `created_at` |
| `camera_module_config` | Per-camera module enable flags + overrides | `PK(camera_uid, module_name)`, `enabled`, `config_json` |
| `location_log` | Where a person was last seen | `id`, `person_id`, `zone_id`, `camera_id`, `timestamp` |
| `system_settings` | Free-form key/value settings | `key PK`, `value`, `description`, `updated_at` |

Embeddings are stored as `BLOB`: `512 * sizeof(float)` bytes, little-endian, exactly as produced by
the recognition model. `FaceDatabase::validateEmbeddingSize()` rejects anything else with
`face embedding must contain exactly 512 values`, which is also why the FFI returns
`Invalid embedding size (must be 512)` instead of writing a partial vector.

## Notable schema properties

These are deliberate observations, not typos in this document:

- **Foreign keys are declared but not enforced.** `PRAGMA foreign_keys` is never turned on by the
  application (it is only toggled inside the two migration scripts), and SQLite defaults it to `OFF`
  per connection. Referential integrity is therefore maintained by the delete paths in code
  (`deletePerson` removes reference embeddings and attendance rows), not by the engine.
- **No indexes beyond primary/unique keys.** Every hot query filters on `person_id`, `date`,
  `timestamp` or `camera_uid`. On a busy site (`raw_detections`, `location_log`, `attendance_logs`
  grow continuously) the useful additions are:
  ```sql
  CREATE INDEX IF NOT EXISTS idx_raw_detections_ts      ON raw_detections(timestamp);
  CREATE INDEX IF NOT EXISTS idx_attendance_person_date ON attendance_logs(person_id, timestamp);
  CREATE INDEX IF NOT EXISTS idx_location_person        ON location_log(person_id, timestamp);
  CREATE INDEX IF NOT EXISTS idx_work_sessions_date     ON work_sessions(date);
  ```
  They are *not* committed here, because adding an index to a live deployment needs a measured
  before/after, and I will not ship a "performance improvement" I did not measure.
- **No retention policy is implemented.** `raw_detections`, `location_log` and the crop images on
  disk grow forever. [PRIVACY.md](PRIVACY.md#4-what-a-real-deployment-must-add) describes what a real deployment must prune;
  `scripts/purge_data.sh` gives the SQL.
- **`cameras.password` is plaintext.** See [SECURITY.md](../SECURITY.md).

## Migration policy

Two mechanisms coexist, and it is important to know which does what:

1. **In-code, idempotent, automatic.** On open, `FaceDatabase` inspects `PRAGMA table_info(<t>)` and
   applies additive changes itself (`ALTER TABLE ... ADD COLUMN ...` inside a `try`), e.g.
   `raw_detections.confidence`, `cameras.username/password/stream_uri/...`. This is what keeps an old
   database usable after an upgrade, and it only ever *adds* columns.
2. **`migrations/*.sql`, manual.** The shipped files document a schema change and the exact
   table-rebuild sequence for SQLite's `ALTER TABLE` limits (create `_new`, copy, drop, rename):
   - `20260512_add_module_config_and_access_levels.sql` — adds `persons.access_level`,
     `zones.required_access_level`, creates `camera_module_config`.
   - `20260526_add_stream_enabled_to_cameras.sql` — separates "preview is on" from "AI is on".

   **Nothing in the binary reads `migrations/`.** Apply them yourself, in filename order, on a copy:
   ```bash
   cp attendance.db attendance.db.bak
   for f in migrations/*.sql; do echo "== $f"; sqlite3 attendance.db < "$f"; done
   ```
   Each file is wrapped in `BEGIN TRANSACTION`/`COMMIT` and uses `IF NOT EXISTS`, so re-running is
   harmless. A `schema_version` table + a runner that records applied files is the obvious next step
   and is on the [roadmap](../README.md#roadmap); until then "the code and the .sql agree" is a human
   responsibility, which is exactly why both live in this repository.

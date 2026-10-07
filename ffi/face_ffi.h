#ifndef FACE_FFI_H
#define FACE_FFI_H

/**
 * @file face_ffi.h
 * @brief Stable C ABI over the C++ recognition/attendance backend
 *        (`libface_backend.so`).
 *
 * This is the only supported way for non-C++ hosts (and for the HTTP layer in
 * `http/http_server.cpp`, which is built on these same functions) to drive the
 * pipeline. The rules below are part of the ABI and are not expected to change.
 *
 * Conventions
 * -----------
 * - **Handles** are opaque pointers. `*_create`/`*_open` return `NULL` on
 *   failure; `*_destroy`/`*_close` accept `NULL` and are no-ops for it.
 * - **Return codes**: functions returning `int` return `0` on success and a
 *   negative value on failure; functions returning `int64_t` return a row id or
 *   `-1`; functions returning `char*` return `NULL` on failure.
 * - **Errors**: after any failure, call `face_last_error()` for a
 *   thread-local-ish human-readable message. No C++ exception ever crosses this
 *   boundary — every entry point catches.
 * - **Memory**: any `char*`/`uint8_t*` returned by this API is heap allocated by
 *   the library and must be released with `face_free_string()` /
 *   `face_free_bytes()`. Never `free()` or `delete[]` them from the host: the
 *   library may live in another translation unit or allocator.
 * - **JSON payloads**: string arguments named `*_json` and all returned strings
 *   are UTF-8 JSON (except `face_last_error`). Field names use lowerCamelCase.
 * - **Embeddings** are exactly 512 little-endian `float32` values, L2-normalised
 *   before storage, which is why the index uses `IndexFlatIP` as a cosine
 *   search. Passing a different length fails with `Invalid embedding size`.
 * - **Threading**: `face_init`/`face_cleanup` are process-wide and not
 *   reentrant. Per-handle calls are safe from different threads as long as one
 *   handle is not used concurrently from two threads; camera handles and the
 *   preview hub are internally synchronised.
 * - **Camera credentials**: camera JSON is *write-only* for `username`/
 *   `password`; reads report `hasPassword` so a UI can show "configured"
 *   without ever seeing the secret (see SECURITY.md).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handles
typedef void* FacePipelineHandle;
typedef void* FaceDatabaseHandle;
typedef void* AttendanceManagerHandle;
typedef void* CameraManagerHandle;  ///< wraps camera::CameraManager
typedef void* CameraStreamerHandle; ///< wraps camera::CameraStreamer

// ========== Init / Cleanup ==========

int face_init(const char* path_config, const char* path_db);
void face_cleanup(void);

// ========== FacePipeline ==========

FacePipelineHandle face_pipeline_create(void);
void face_pipeline_destroy(FacePipelineHandle pipeline);
char* face_process_frame(FacePipelineHandle pipeline, const uint8_t* image_data, size_t image_len);
char* face_process_frame_raw(FacePipelineHandle pipeline, const uint8_t* bgr_data, int rows,
                             int cols, int step);

// ========== FaceDatabase ==========

FaceDatabaseHandle face_db_open(const char* path_db);
void face_db_close(FaceDatabaseHandle db);

int64_t face_db_add_person(FaceDatabaseHandle db, const char* name, const char* code);
char* face_db_get_person(FaceDatabaseHandle db, int64_t person_id);
char* face_db_get_all_persons(FaceDatabaseHandle db);
int face_db_update_person(FaceDatabaseHandle db, int64_t person_id, const char* name,
                          const char* code);
int face_db_delete_person(FaceDatabaseHandle db, int64_t person_id);
int face_db_add_reference_embedding(FaceDatabaseHandle db, int64_t person_id, const float* emb,
                                    size_t emb_size);
int face_db_add_unknown_face(FaceDatabaseHandle db, const float* emb, size_t emb_size,
                             const char* track_id, const char* camera_uid, int64_t timestamp,
                             const char* image_path);
char* face_db_get_all_references(FaceDatabaseHandle db);
int64_t face_db_add_raw_detection(FaceDatabaseHandle db, const char* track_id, const float* emb,
                                  size_t emb_size, const char* image_path, int64_t timestamp,
                                  const char* status, float confidence);
void face_db_update_detection_person(FaceDatabaseHandle db, int64_t detection_id,
                                     int64_t person_id);
char* face_db_get_all_detections(FaceDatabaseHandle db);

// Unknown face review
char* face_db_get_unknown_faces(FaceDatabaseHandle db);
int face_db_delete_unknown_face(FaceDatabaseHandle db, int64_t unknown_face_id);
int64_t face_db_enroll_unknown_as_new(FaceDatabaseHandle db, int64_t unknown_face_id,
                                      const char* name, const char* code, int access_level);
int face_db_merge_unknown_with_person(FaceDatabaseHandle db, int64_t unknown_face_id,
                                      int64_t person_id);
void face_db_rebuild_index(void);

// Camera config in DB
int64_t face_db_upsert_camera(FaceDatabaseHandle db, const char* camera_json);
// Configure camera settings and module configs in a single call.
// `camera_uid` is required. `config_json` may contain top-level camera fields
// (name, zoneId, streamEnabled, aiEnabled, type) and an object `modules` mapping moduleName
// -> { enabled: bool, config: {...} }.
int face_db_configure_camera(FaceDatabaseHandle db, const char* camera_uid,
                             const char* config_json);
char* face_db_get_camera(FaceDatabaseHandle db, const char* camera_uid);
char* face_db_get_camera_by_id(FaceDatabaseHandle db, int64_t camera_id);
char* face_db_get_all_cameras(FaceDatabaseHandle db);
int face_db_delete_camera(FaceDatabaseHandle db, const char* camera_uid);
char* face_db_get_camera_module_config(FaceDatabaseHandle db, const char* camera_uid,
                                       const char* module_name);
int face_db_set_camera_module_config(FaceDatabaseHandle db, const char* camera_uid,
                                     const char* module_name, int enabled, const char* config_json);

// Zone management
int64_t face_db_add_zone(FaceDatabaseHandle db, const char* zone_json);
char* face_db_get_zone(FaceDatabaseHandle db, int64_t zone_id);
char* face_db_get_all_zones(FaceDatabaseHandle db);
int face_db_set_zone_required_access_level(FaceDatabaseHandle db, int64_t zone_id,
                                           int required_access_level);
int face_db_update_zone(FaceDatabaseHandle db, int64_t zone_id, const char* name,
                        const char* description);
int face_db_delete_zone(FaceDatabaseHandle db, int64_t zone_id);

// Schedule / settings
int face_db_upsert_person_schedule(FaceDatabaseHandle db, const char* schedule_json);
char* face_db_get_person_schedule(FaceDatabaseHandle db, int64_t person_id);
void face_db_set_setting(FaceDatabaseHandle db, const char* key, const char* value,
                         const char* description);
char* face_db_get_setting(FaceDatabaseHandle db, const char* key);

// Reports
char* face_db_get_daily_summary(FaceDatabaseHandle db, int64_t person_id, const char* date);
char* face_db_get_timeline(FaceDatabaseHandle db, int64_t person_id, const char* date);
char* face_db_get_tardiness_list(FaceDatabaseHandle db, const char* date);
char* face_db_get_current_location(FaceDatabaseHandle db, int64_t person_id);

// ========== AttendanceManager ==========

AttendanceManagerHandle face_attendance_create(FaceDatabaseHandle db, const char* defaults_json);
void face_attendance_destroy(AttendanceManagerHandle att);
char* face_attendance_mark_arrival(AttendanceManagerHandle att, const char* track_id,
                                   int64_t person_id, int64_t timestamp);
char* face_attendance_mark_departure(AttendanceManagerHandle att, const char* track_id,
                                     int64_t person_id, int64_t timestamp);
void face_attendance_remove_track(AttendanceManagerHandle att, const char* track_id);
char* face_attendance_get_status(AttendanceManagerHandle att, const char* track_id);

// ========== CameraManager — Discovery ==========

// Create a CameraManager instance (stateless; can share one across the app).
CameraManagerHandle face_camera_manager_create(void);
void face_camera_manager_destroy(CameraManagerHandle mgr);

// Discover all cameras on the system.
// deep_search = 1 also scans RTSP subnet (slow); 0 = local + ONVIF only.
// Returns a malloc'd JSON array of DeviceInfo objects; caller frees with face_free_string.
// Each element: { "devicePath": str, "streamUri": str, "cardName": str,
//                 "driver": str, "type": "LOCAL"|"RTSP"|"ONVIF", "index": int }
char* face_camera_discover(CameraManagerHandle mgr, int deep_search);

// ========== CameraStreamer — Live frames ==========

// NOTE: These APIs are kept for backwards compatibility but the recommended
// flow is to use camera activity toggles and database-driven results.
CameraStreamerHandle face_camera_open(CameraManagerHandle mgr, const char* device_path_or_uri);
void face_camera_close(CameraStreamerHandle streamer);
uint8_t* face_camera_get_frame_bgr(CameraStreamerHandle streamer, int* out_rows, int* out_cols,
                                   size_t* out_step);

// ========== Camera/Module helpers ==========
char* face_db_get_available_modules(FaceDatabaseHandle db);
char* face_db_get_module_config(FaceDatabaseHandle db, const char* camera_uid,
                                const char* module_name);
int face_db_set_module_config(FaceDatabaseHandle db, const char* camera_uid,
                              const char* module_name, const char* config_json, int enabled);
int face_db_set_person_access_level(FaceDatabaseHandle db, int64_t person_id, int access_level);
int face_db_set_zone_required_access_level(FaceDatabaseHandle db, int64_t zone_id,
                                           int required_access_level);
int face_db_set_camera_active(FaceDatabaseHandle db, const char* camera_uid, int active);
int face_db_set_camera_ai_enabled(FaceDatabaseHandle db, const char* camera_uid, int enabled);

// ========== Preview subscription helpers ==========
int face_preview_subscribe(const char* camera_uid);
int face_preview_unsubscribe(const char* camera_uid);
uint8_t* face_preview_get_latest_frame_jpeg(const char* camera_uid, size_t* out_len, int* out_width,
                                            int* out_height, float* out_fps);
uint8_t* face_preview_get_latest_frame_bgr(const char* camera_uid, size_t* out_len, int* out_width,
                                           int* out_height, int* out_stride, float* out_fps);

// ========== Helpers ==========

void face_free_string(char* str);
void face_free_bytes(uint8_t* ptr); ///< free buffers returned by face_preview_get_latest_frame_*
const char* face_last_error(void);

#ifdef __cplusplus
}
#endif

#endif // FACE_FFI_H

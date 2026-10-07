# C FFI: `libface_backend.so`

The backend is a shared library first and an HTTP server second. The public contract is
[`ffi/face_ffi.h`](../ffi/face_ffi.h) — 71 `extern "C"` entry points, built as `face_backend`
(`SOVERSION` = major version) by the top-level `CMakeLists.txt`.

## Rules of the boundary

| Rule | Why |
|------|-----|
| Opaque handles (`void*`), never dereferenced by the host | Keeps C++ types (and their ABI/ODM hazards) on one side. |
| `int` return: `0` = ok, negative = error; `int64_t`: id or `-1`; pointers: `NULL` = error | One convention instead of per-function surprises. |
| Every returned string is `malloc`'d UTF-8 JSON, freed with `face_free_string()`; binary buffers with `face_free_bytes()` | The library and the host may use different allocators; `free()` from the host is undefined behaviour. |
| `face_last_error()` after any failure | Exceptions never cross the boundary — every entry point catches `std::exception` (and the riskiest paths catch `...`). |
| Embeddings are exactly `512 × float32` | Validated in both the FFI layer and `FaceDatabase::validateEmbeddingSize()`; the index is `IndexFlatIP`, so vectors are compared as cosine similarity. |
| `face_init`/`face_cleanup` are process-wide, called once | They own the preview hub and the orchestrator singletons (`g_previewHub`, `g_orchestrator`). |
| One handle used by one thread at a time | Internally shared structures are mutex-guarded; per-handle calls are not. |
| Camera `password` is write-only | `hasPassword` is what reads return. |

## Function groups

| Group | Functions | Notes |
|-------|-----------|-------|
| Lifecycle | `face_init`, `face_cleanup`, `face_last_error` | `face_init(config_path, db_path)` returns non-zero with an error message on failure. |
| Pipeline | `face_pipeline_create/destroy`, `face_process_frame`, `face_process_frame_raw` | `frame` takes encoded image bytes (JPEG/PNG), `frame_raw` takes a packed BGR buffer (`rows`, `cols`, `step`). Both answer with a JSON array. |
| Persons | `face_db_add_person`, `face_db_get_person`, `face_db_get_all_persons`, `face_db_update_person`, `face_db_delete_person`, `face_db_set_person_access_level`, `face_db_upsert_person_schedule`, `face_db_get_person_schedule` | |
| Embeddings | `face_db_add_reference_embedding`, `face_db_get_all_references`, `face_db_rebuild_index` | Rebuild is also triggered automatically by enrol/merge/delete so running workers pick up index changes. |
| Review queue | `face_db_add_unknown_face`, `face_db_get_unknown_faces`, `face_db_delete_unknown_face`, `face_db_enroll_unknown_as_new`, `face_db_merge_unknown_with_person` | `..._enroll_unknown_as_new` creates the person, links the embedding and removes the queue row. |
| Detections | `face_db_add_raw_detection`, `face_db_update_detection_person`, `face_db_get_all_detections` | |
| Cameras | `face_db_upsert_camera`, `face_db_configure_camera`, `face_db_get_camera`, `face_db_get_camera_by_id`, `face_db_get_all_cameras`, `face_db_delete_camera`, `face_db_set_camera_active`, `face_db_set_camera_ai_enabled`, `face_db_get_camera_module_config`, `face_db_set_camera_module_config` | `configure_camera` handles camera fields + `modules` map in one call. |
| Camera manager & discovery | `face_camera_manager_create`, `face_camera_manager_destroy`, `face_camera_open`, `face_camera_close`, `face_camera_discover`, `face_camera_get_frame_bgr` | `face_camera_discover(mgr, deep_search)` returns the same device JSON the HTTP discovery endpoint exposes; the open/close pair drives a single streamer directly. |
| Preview | `face_preview_subscribe`, `face_preview_unsubscribe`, `face_preview_get_latest_frame_jpeg`, `face_preview_get_latest_frame_bgr` | The preview getters are how an MJPEG responder (or any renderer) reads frames without touching a capture thread. |
| Module config | `face_db_get_available_modules`, `face_db_get_module_config`, `face_db_set_module_config` | Global (not per-camera) module state; `GET /api/modules` reads through these. |
| Attendance status | `face_attendance_get_status`, `face_attendance_remove_track` | `remove_track` forgets a track's session state, e.g. after a camera restart. |
| Zones | `face_db_add_zone`, `face_db_get_zone`, `face_db_get_all_zones`, `face_db_update_zone`, `face_db_delete_zone`, `face_db_set_zone_required_access_level` | |
| Attendance | `face_attendance_create/destroy`, `face_attendance_mark_arrival`, `face_attendance_mark_departure`, `face_db_get_daily_summary`, `face_db_get_timeline`, `face_db_get_tardiness_list`, `face_db_get_current_location` | |
| Settings | `face_db_set_setting`, `face_db_get_setting` | |

The detection JSON returned by `face_process_frame` carries, per face: bounding box, confidence,
track id, person id/name (when recognised) and the `embedding` array used for enrolment.

## Using it from C

```c
#include "ffi/face_ffi.h"
#include <stdio.h>

int main(void) {
    if (face_init("core/config/config.json", "attendance.db") != 0) {
        fprintf(stderr, "init failed: %s\n", face_last_error());
        return 1;
    }

    FacePipelineHandle p = face_pipeline_create();
    FILE *f = fopen("portrait.jpg", "rb");
    static unsigned char buf[4 << 20];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);

    char *json = face_process_frame(p, buf, n);   /* the raw response shape */
    printf("%s\n", json ? json : face_last_error());
    face_free_string(json);
    face_pipeline_destroy(p);

    face_cleanup();
}
```

```bash
gcc example.c -I. -Lbuild -lface_backend -o example
LD_LIBRARY_PATH=build ./example
```

## Using it from Python

No generator needed — `ctypes` covers it, which is the fastest way to script the engine or run a
batch over an image directory:

```python
import ctypes, json, pathlib

lib = ctypes.CDLL("build/libface_backend.so")
lib.face_init.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
lib.face_init.restype = ctypes.c_int
lib.face_pipeline_create.restype = ctypes.c_void_p
lib.face_process_frame.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
lib.face_process_frame.restype = ctypes.c_char_p
lib.face_free_string.argtypes = [ctypes.c_char_p]

assert lib.face_init(b"core/config/config.json", b"attendance.db") == 0, \
    ctypes.string_at(lib.face_last_error()).decode()

pipeline = lib.face_pipeline_create()
try:
    for image in sorted(pathlib.Path("frames").glob("*.jpg")):
        raw = lib.face_process_frame(pipeline, image.read_bytes(), image.stat().st_size)
        for face in json.loads(raw or b"[]"):
            print(image.name, face.get("trackId"), face.get("personName"), face.get("confidence"))
        lib.face_free_string(raw)
finally:
    lib.face_pipeline_destroy(pipeline)
    lib.face_cleanup()
```

Reading a live preview the same way:

```python
lib.face_preview_get_latest_frame_jpeg.argtypes = [
    ctypes.c_char_p,
    ctypes.POINTER(ctypes.c_size_t),   # out_len
    ctypes.POINTER(ctypes.c_int),      # out_width
    ctypes.POINTER(ctypes.c_int),      # out_height
    ctypes.POINTER(ctypes.c_float),    # out_fps
]
lib.face_preview_get_latest_frame_jpeg.restype = ctypes.POINTER(ctypes.c_ubyte)

length, w, h, fps = ctypes.c_size_t(), ctypes.c_int(), ctypes.c_int(), ctypes.c_float()
ptr = lib.face_preview_get_latest_frame_jpeg(b"rtsp://cam01/stream1",
                                            ctypes.byref(length), ctypes.byref(w),
                                            ctypes.byref(h), ctypes.byref(fps))
frame = ctypes.string_at(ptr, length.value)      # copy, then release
lib.face_free_bytes(ptr)
```

## Adding a function to the ABI

1. Declare it in `ffi/face_ffi.h` with the conventions above documented in the header.
2. Implement it in `ffi/face_ffi.cpp`: `try`/`catch` → `setError(...)` → negative return; allocate
   results through `copyString()` so the host can always free them with `face_free_string()`.
3. If it is data the UI needs, add the thin handler in `http/http_server.cpp` and document it in
   [API.md](API.md). Keep the rule that handlers call *only* `face_*` functions.
4. Never expose a C++ type, a reference into a container, or a pointer the host must `delete`.

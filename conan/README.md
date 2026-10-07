# First-party Conan recipes

Three packages that this project owns are built from here instead of being consumed from a public
Conan remote. Each lives in `conan/recipes/<name>/` with `version = "1.0.0"` declared in its
`conanfile.py`, so `conan create` needs no extra flags:

```bash
conan create conan/recipes/byte-track
conan create conan/recipes/log-factory
conan create conan/recipes/tensorrt-runtime
```

| Recipe | Provides | Upstream dependencies | Notes |
|--------|----------|----------------------|-------|
| [`recipes/byte-track`](recipes/byte-track) | `byte_track::byte_track` (`ByteTrack` static lib) | `eigen/3.4.0`, `libuuid/1.0.3` | Multi-object tracker with a Kalman filter and Hungarian (JV) assignment. Headers in `include/ByteTrack/`, sources in `src/`, `windows_compat/` for the MSVC uuid shim. |
| [`recipes/log-factory`](recipes/log-factory) | `log_factory::log_factory` (header-only) | `fmt/11.1.4`, **`log-entry/1.0.0`** | Structured log entries and console colour formatting. Keeps the vendor's `project/` tree layout on purpose (see below). |
| [`recipes/tensorrt-runtime`](recipes/tensorrt-runtime) | `tensorrt_runtime::tensorrt_runtime` (`TensorRTRuntime`) | `log-factory`, `nlohmann_json`, optional `opencv` | Engine loading, tensor/output/float-image wrappers used by `services/inference`. |

`conanfile.txt` at the repository root is what pins these into the application build, together with the
OpenCV options that the code actually needs (`bgsegm`, `with_v4l`, `with_gtk`, `with_ffmpeg`).

## Why these live in the repository

- They are internal engineering work, not published upstream, so there is no remote to pull them from.
- The build must be reproducible for a reviewer without access to any private artifact repository.
- ByteTrack in particular is a third-party algorithm, but the copy here is patched for the way the
  pipeline feeds it (and vendoring it avoids a moving target).

## `log-entry` is missing, on purpose

`log-factory` requires `log-entry/1.0.0`, a **proprietary** package that is not redistributed with
this repository. Consequences, stated plainly:

- `conan install .` in a fresh clone fails while resolving the graph.
- `core/config/Config.h` includes `<base_component/monitoring/log/log_utils.h>`, and
  `log_factory.h` / `log_error.h` include `log_entry.h`, so the compile fails too.

To build, either provide your own `log-entry` recipe at `conan/recipes/log-entry/` (that directory is
`.gitignore`d) or write a minimal stub for the handful of declarations actually used. See
[docs/BUILDING.md](../docs/BUILDING.md#4-optional-internal-dependencies).

## Recipe conventions

- **One package per directory, no version in the path.** The layout previously used here
  (`byte-track/1.0.0-2-71f99ef7/`, `tensorrt-runtime/test-0.1/`) encoded internal artifact revisions in
  directory names, which made the documented build commands wrong and the tree unreadable. Version
  revisions belong in `version` + `user/channel`, not in paths.
- **`exports_sources` must list every directory the recipe builds from** — a file that is not exported
  is silently absent in the build folder, which is one of the most confusing Conan failures there is.
- **`package_type` is declared** (`library` vs `header-library`) so Conan handles header-only
  packages correctly (no build, package-id independent of compiler).
- **Upstream trees are not re-flowed.** `log-factory` and `tensorrt-runtime` keep their internal
  `project/` nesting because re-shaping them cannot be validated without the CUDA/TensorRT toolchain.
  `byte-track`'s `files/` was renamed to `include/` because that one is a pure path change and every
  reference is inside this repository (CMake source list, `install(DIRECTORY ...)`, the `package()`
  copy and the `#include "../include/ByteTrack/..."` lines in `src/`).
- **`build_config.yaml`** in `log-factory` toggles the example target (`build_example: false`) so the
  recipe does not try to build a sample that needs the missing `log-entry`.

# Building

Target platform: **Ubuntu 22.04 / 24.04 on x86-64 with an NVIDIA GPU.** The code uses Linux-only
APIs (`linux/videodev2.h`, `getifaddrs`, `localtime_r`, `gmtime_r`) and TensorRT engines that are
built for a specific GPU, so there is no supported Windows/macOS/CPU build.

```
apt packages ─┐
CUDA + TensorRT ┤
Conan recipes (conan/recipes/*) ─┼─▶ conan install ─▶ cmake configure ─▶ cmake --build ─▶ 3 artifacts
models (.onnx → .engine) ─┤        (build/libface_backend.so, build/faceauth, build/faceauth-admin)
config + license ─────────┘
```

## 0. What you need

| Requirement | Version | Checked with |
|-------------|---------|--------------|
| GCC / Clang with C++20 | GCC ≥ 11 / Clang ≥ 14 | `g++ --version` |
| CMake | ≥ 3.26 | `cmake --version` |
| Conan | 2.x | `conan --version` |
| CUDA Toolkit | 12.x recommended | `nvcc --version` |
| TensorRT | 8.x / 10.x | `trtexec --version` |
| OpenCV, GStreamer, FFmpeg, SQLite, FAISS, pugixml, nlohmann/json | via Conan (pinned in `conanfile.txt`) | `conan graph info .` |

## 1. System packages

```bash
sudo apt update
sudo apt install -y build-essential cmake python3 python3-pip git \
    libssl-dev libsqlite3-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav
```

Install the CUDA Toolkit and TensorRT following NVIDIA's instructions for your driver, then make
sure the tooling is visible:

```bash
export PATH=/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:/opt/tensorrt/lib:$LD_LIBRARY_PATH
nvcc --version && trtexec --help >/dev/null && echo "CUDA/TensorRT visible"
```

## 2. Conan

```bash
python3 -m pip install conan
conan profile detect            # remove and re-run if you change compiler versions
```

Dependencies are pinned in [`conanfile.txt`](../conanfile.txt) (`opencv/4.12.0`, `faiss/1.12.0`,
`gstreamer/1.28.1`, `sqlite3/3.53.0`, `pugixml/1.15`, `nlohmann_json/3.11.3`, `ffmpeg/4.4.6` plus
the three local recipes). OpenCV options are set there for a reason: `bgsegm` (the motion module uses
it), `with_v4l`, `with_gtk` and `with_ffmpeg`; `with_webp`/`with_tiff`/`with_wayland` are off to keep
the graph small.

## 3. First-party recipes

Three packages live in this repository under `conan/recipes/` and must be created before the
top-level install resolves them (details in [`conan/README.md`](../conan/README.md)):

```bash
conan create conan/recipes/byte-track        # ByteTrack + Kalman + Hungarian (needs Eigen, libuuid)
conan create conan/recipes/log-factory       # header-only logging factory (needs fmt, log-entry)
conan create conan/recipes/tensorrt-runtime  # TensorRT engine/tensor wrapper
```

Each recipe now declares `version = "1.0.0"` so the plain command above works; the versioned
directory names that used to encode internal artifact hashes (`1.0.0-2-71f99ef7`, `test-0.1`) are
gone.

## 4. Optional: internal dependencies

`log-factory` requires **`log-entry`**, a proprietary package that is *not* redistributed here, and
`core/config/Config.h` includes `<base_component/monitoring/log/log_utils.h>` from it. Without it
`conan install` fails to resolve the graph. Two ways forward:

- **Provide your own** `log-entry` recipe (`conan/recipes/log-entry/`, which is `.gitignore`d) —
  that is the supported route and what the original deployment does.
- **Stub it locally** if you only want to read and extend the code. The interface actually used is
  small: a `LogCallback` alias plus `xbox::base_component::monitoring::log::*` types consumed by
  `Config`, `log_factory.h` and `log_error.h`. A minimal stub is the usual first contribution to a
  clone of this repository.

A CPU-only path does not exist: the detectors are TensorRT engines and `CMakeLists.txt` requires
`CUDAToolkit`, `nvinfer` and `faiss`.

## 5. Configure and build

```bash
conan install . --output-folder=build --build=missing
cmake --preset conan-release          # or: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Artifacts:

| Output | What it is |
|--------|-----------|
| `build/libface_backend.so` | the shared library behind the C ABI (`SOVERSION` 1) |
| `build/faceauth` | full application: discovery + pipeline + HTTP API |
| `build/faceauth-admin` | HTTP API only; cameras added at runtime |

`-D`-style knobs worth knowing: `CMAKE_BUILD_TYPE=RelWithDebInfo` for profiling, and
`CMAKE_EXPORT_COMPILE_COMMANDS` is already on so `clangd` works out of the box.

### Tests

The unit tests have no third-party dependencies, so they build on any machine:

```bash
cmake -S tests -B build/tests
cmake --build build/tests -j"$(nproc)"
ctest --test-dir build/tests --output-on-failure
```

They are also wired into the top-level build (`enable_testing()` + `add_subdirectory(tests)`).

## 6. Models

Put the ONNX models in `core/models/`, then build engines for **your** GPU (engines are not portable
across TensorRT versions or GPU architectures):

```bash
cd core/models
trtexec --onnx=det_500m_FP32.onnx  --saveEngine=det_500m_FP32.engine  --fp16
trtexec --onnx=webface_r50.onnx    --saveEngine=webface_r50.engine     --fp16
trtexec --onnx=headdetection_v12.onnx --saveEngine=headdetection_v12.engine --fp16
```

The detection model's multiple output heads must be reordered before conversion — see
[`core/models/reorder.py`](../core/models/reorder.py) and [MODELS.md](MODELS.md).

## 7. Configuration

```bash
cp core/config/config.example.json core/config/config.json
cp core/config/license.example.json core/config/license.json     # see CONFIGURATION.md
```

`config.json` and `license.json` are ignored by git on purpose: only the `*.example.json` templates
are tracked. Every key is documented in [CONFIGURATION.md](CONFIGURATION.md).

## 8. Run

```bash
./build/faceauth                 # config: core/config/config.json, db: attendance.db
./build/faceauth --help          # usage and options
./build/faceauth my.json prod.db # explicit paths
./build/faceauth-admin           # API only, port 8080
```

On startup the application loads the config, opens/migrates the SQLite database, discovers local
(V4L2) and ONVIF cameras, registers them, starts the orchestrator and preview hub, and serves the
API on `0.0.0.0:8080`. Without `$DISPLAY` it runs headless — the recommended mode on a server — and
with one it also opens OpenCV preview windows.

Smoke test:

```bash
curl -s localhost:8080/api/health
curl -s localhost:8080/api/cameras | python3 -m json.tool
```

## 8b. Running as a service

A hardened unit for a dedicated `faceauth` user — this is the configuration the deployment used, and
the privilege set is the point of it rather than the paths:

```ini
# /etc/systemd/system/faceauth.service
[Unit]
Description=FaceAuth Attendance backend
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=faceauth
Group=faceauth
WorkingDirectory=/opt/faceauth
ExecStart=/opt/faceauth/build/faceauth /opt/faceauth/core/config/config.json /var/lib/faceauth/attendance.db
Restart=on-failure
RestartSec=5

NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/lib/faceauth /opt/faceauth/crops
ProtectKernelTunables=true
ProtectControlGroups=true
RestrictSUIDSGID=true
LockPersonality=true
# Needed only for V4L2 local capture:
DeviceAllow=/dev/video0 rw
DeviceAllow=/dev/video1 rw

[Install]
WantedBy=multi-user.target
```

```bash
sudo sysctl -w net.core.rmem_max=4194304      # ONVIF multicast discovery wants a bigger UDP buffer
sudo systemctl daemon-reload && sudo systemctl enable --now faceauth
journalctl -u faceauth -f
```

Keep the bind address on a trusted VLAN, or front it with nginx and point the service at
`127.0.0.1` only (see [SECURITY.md](../SECURITY.md#hardening-checklist-for-anything-real)).

## 9. CI

[`../.github/workflows/ci.yml`](../.github/workflows/ci.yml) runs unit tests, formatting and the
hygiene checks on `ubuntu-latest`, which needs no GPU. The CUDA build job is
`workflow_dispatch`-only so it does not fail on runners without TensorRT: to enable it, register a
self-hosted runner with the labels `gpu` and `cuda`, and CUDA/TensorRT available in its environment.

## 10. Troubleshooting

| Symptom | Cause and fix |
|---------|---------------|
| `Version not defined in conan_file` | Use the recipe paths as documented; the recipes now carry `version = "1.0.0"`, so no `--version` flag is needed. |
| `log-entry/1.0.0: not found in remotes` | Expected in a fresh clone. See [step 4](#4-optional-internal-dependencies). |
| `Could not find a package configuration file provided by "tensorrt_runtime"` | The recipes were not created before `conan install`; re-run step 3 then step 5. |
| `nvinfer` not found | TensorRT `lib` is not on `LD_LIBRARY_PATH`/`CMAKE_PREFIX_PATH`. |
| `cannot open shared object file: libfaiss.so` | FAISS was built but not installed into the package folder; re-run `conan install . --build=missing`. |
| Engine loads then immediately fails | The `.engine` was built on another GPU/TensorRT version. Rebuild it with `trtexec` on the target machine. |
| No cameras found | Local: the user needs `video` group membership (`getent group video`). ONVIF: the camera and the host must share a broadcast domain, or add the camera manually via `POST /api/cameras`. |
| Preview shows nothing but recognition works | `stream_enabled` is off for that camera: `PUT /api/cameras/{id}/active {"active":true}`. |
| `403` from `/api/files/...` | The path escapes the configured crops directory — that is the intended behaviour. |

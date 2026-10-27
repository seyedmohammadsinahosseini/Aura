# ==============================================================================
# Backend Dockerfile: FaceAuthentication admin_server
# C++20 / CUDA / TensorRT / OpenCV / FAISS
# ==============================================================================
# Build:  docker build -t face-auth-backend .
# Run:    docker run --gpus all --network host \
#           -v /path/to/models:/app/core/models \
#           face-auth-backend
#         (--network host is required for LAN camera discovery; the server
#          then listens on the host directly, no -p port mapping needed)
#
# Size notes (final image was 6.6 GB before these optimizations):
#  - Binaries and collected shared libraries are stripped (no debug symbols).
#  - TensorRT *PTX* + *sm75* builder resources are shipped (~230 MB +
#    ~113 MB) instead of all per-SM resources (~1.85 GB). On a sm75 GPU
#    (GeForce MX450/Turing) TensorRT requires the sm75 resource to create
#    a builder — it does NOT fall back to PTX. Keep sm75 for this machine
#    and PTX for portability; to target a different GPU, change the
#    libnvinfer_builder_resource copy in the collection step below.
#  - System libraries already provided by the runtime base image (glibc,
#    libstdc++, libgcc, CUDA runtime, ...) are NOT copied again.
#  - The Conan cache lives in a BuildKit cache mount: it never bloats image
#    layers and stays cached between builds (much faster rebuilds).
#  - Only config files are baked in; models and attendance.db are mounted
#    at runtime (see docker-compose.yml).
# ==============================================================================

# ---- Stage 1: Build ----
FROM nvidia/cuda:12.6.3-cudnn-devel-ubuntu24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# System build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git curl \
    python3 python3-pip python3-venv \
    pkg-config wget gnupg2 \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    && rm -rf /var/lib/apt/lists/*

# Install TensorRT development packages — PINNED to 11.3.0.
# TensorRT engine files (.engine) only deserialize with the exact version
# that built them; this project's engines are built with TensorRT 11.3.0.
# Do NOT let apt resolve a newer version (e.g. 11.4) silently — that breaks
# engine loading and AI startup. All sub-packages are pinned explicitly so
# apt cannot mix 11.3 dev headers with 11.4 runtime libs.
ARG TENSORRT_VERSION=11.3.0.99-1+cuda12.9
RUN apt-get update && \
    (apt-get install -y --no-install-recommends \
        libnvinfer-dev=$TENSORRT_VERSION \
        libnvinfer-headers-dev=$TENSORRT_VERSION \
        libnvinfer-safe-headers-dev=$TENSORRT_VERSION \
        libnvinfer11=$TENSORRT_VERSION \
        libnvinfer-plugin-dev=$TENSORRT_VERSION \
        libnvinfer-headers-plugin-dev=$TENSORRT_VERSION \
        libnvinfer-plugin11=$TENSORRT_VERSION \
        libnvonnxparsers-dev=$TENSORRT_VERSION \
        libnvonnxparsers11=$TENSORRT_VERSION 2>/dev/null || \
    (rm -f /etc/apt/sources.list.d/cuda*.list /etc/apt/sources.list.d/cuda*.sources && \
     wget -qO /tmp/cuda-keyring.deb \
        https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-keyring_1.1-1_all.deb && \
     dpkg -i /tmp/cuda-keyring.deb && \
     apt-get update && \
     apt-get install -y --no-install-recommends \
        libnvinfer-dev=$TENSORRT_VERSION \
        libnvinfer-headers-dev=$TENSORRT_VERSION \
        libnvinfer-safe-headers-dev=$TENSORRT_VERSION \
        libnvinfer11=$TENSORRT_VERSION \
        libnvinfer-plugin-dev=$TENSORRT_VERSION \
        libnvinfer-headers-plugin-dev=$TENSORRT_VERSION \
        libnvinfer-plugin11=$TENSORRT_VERSION \
        libnvonnxparsers-dev=$TENSORRT_VERSION \
        libnvonnxparsers11=$TENSORRT_VERSION)) && \
    rm -rf /var/lib/apt/lists/*

# Install Conan 2.x (isolated in a venv)
RUN python3 -m venv /opt/conan-venv && \
    /opt/conan-venv/bin/pip install --no-cache-dir conan && \
    ln -s /opt/conan-venv/bin/conan /usr/local/bin/conan

WORKDIR /src

# ------------------------------------------------------------------------------
# Dependency layer — only re-runs when conanfile.txt or the local recipes
# change; editing project sources below does NOT re-download/rebuild deps.
# Recipe list must stay in sync with LOCAL_RECIPES in build.sh.
# The Conan cache is a BuildKit cache mount (persisted across builds,
# never stored in image layers).
# ------------------------------------------------------------------------------
COPY conanfile.txt build.sh ./
COPY conan-custom/ ./conan-custom/

RUN --mount=type=cache,target=/root/.conan2 \
    conan profile detect --force && \
    conan create conan-custom/log-entry/1.0.0-4-83193b47        --name=log-entry        --version=1.0.0-4-83193b47  -pr:h default --build=missing -c tools.system.package_manager:mode=install && \
    conan create conan-custom/log-factory/1.0.0-1-56934db0      --name=log-factory      --version=1.0.0-1-56934db0 -pr:h default --build=missing -c tools.system.package_manager:mode=install && \
    conan create conan-custom/byte-track/1.0.0-2-71f99ef7       --name=byte-track       --version=1.0.0-2-71f99ef7  -pr:h default --build=missing -c tools.system.package_manager:mode=install && \
    conan create conan-custom/tensorrt-runtime/test-0.1         --name=tensorrt-runtime --version=test-0.1        -pr:h default --build=missing -c tools.system.package_manager:mode=install && \
    conan install . -pr:h default --output-folder=build --build=missing -c tools.system.package_manager:mode=install

# ------------------------------------------------------------------------------
# Sources + full build (build.sh reuses the warm Conan cache from above)
# ------------------------------------------------------------------------------
COPY . .
RUN --mount=type=cache,target=/root/.conan2 \
    chmod +x build.sh && PROFILE=default BUILD_DIR=build ./build.sh

# ------------------------------------------------------------------------------
# Collect exactly what the runtime stage needs, stripped:
#  - binaries (strip removes debug symbols, keeps dynamic linkage)
#  - shared libraries resolved by ldd, minus libs the runtime base image ships
#  - Conan-packaged libs (catches dlopen'ed libs; build trees pruned)
#  - TensorRT runtime + plugin lib + PTX builder resource (see header notes)
# ------------------------------------------------------------------------------
RUN --mount=type=cache,target=/root/.conan2 \
    set -e; \
    strip --strip-unneeded build/admin_server build/libface_backend.so.1 2>/dev/null || true; \
    mkdir -p /runtime /runtime-system; \
    cp build/admin_server /runtime/; \
    cp build/libface_backend.so.1 /runtime/; \
    for bin in /runtime/admin_server /runtime/libface_backend.so.1; do \
        ldd "$bin" 2>/dev/null | awk '/=>/ {print $3}' | while read lib; do \
            case "$lib" in \
                *ld-linux*|*/libc.so*|*/libm.so*|*/libpthread.so*|*/libdl.so*|*librt.so*|*libgcc_s.so*|*libstdc++.so*|*libcudart.so*) continue ;; \
            esac; \
            [ -f "$lib" ] && cp -L "$lib" /runtime/ 2>/dev/null || true; \
        done; \
    done; \
    find /root/.conan2/p -path '*/b/*' -prune -o -type f -name '*.so*' -exec cp -L {} /runtime/ \; 2>/dev/null || true; \
    cp -L /usr/lib/x86_64-linux-gnu/libnvinfer_plugin.so.11 /runtime/libnvinfer_plugin.so.11 2>/dev/null || true; \
    cp -L /usr/lib/x86_64-linux-gnu/libnvinfer_builder_resource_ptx.so* /runtime-system/ 2>/dev/null || true; \
    cp -L /usr/lib/x86_64-linux-gnu/libnvinfer_builder_resource_sm75.so* /runtime-system/ 2>/dev/null || true; \
    for f in /runtime/*.so*; do strip --strip-unneeded "$f" 2>/dev/null || true; done; \
    mkdir -p /runtime-bin && mv /runtime/admin_server /runtime-bin/; \
    echo "Collected $(ls /runtime/*.so* 2>/dev/null | wc -l) libs, $(du -sh /runtime | cut -f1) + $(du -sh /runtime-system | cut -f1)"

# ---- Stage 2: Runtime ----
FROM nvidia/cuda:12.6.3-cudnn-runtime-ubuntu24.04

ENV DEBIAN_FRONTEND=noninteractive

# Minimal runtime system dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libgstreamer1.0-0 \
    libgstreamer-plugins-base1.0-0 \
    libgstreamer-plugins-bad1.0-0 \
    libsqlite3-0 \
    libgl1 \
    libglib2.0-0 \
    && rm -rf /var/lib/apt/lists/*

# Copy binary to PATH (kept out of /runtime so it doesn't land in /usr/local/lib)
COPY --from=builder /runtime-bin/admin_server /usr/local/bin/admin_server

# Copy all collected runtime shared libraries
COPY --from=builder /runtime/ /usr/local/lib/
# TensorRT PTX builder resource is loaded from its system library path
# (used only when an engine must be built from ONNX at runtime)
COPY --from=builder /runtime-system/ /usr/lib/x86_64-linux-gnu/

# Copy config files (admin_server looks for core/config/config.json relative to CWD)
COPY --from=builder /src/core/config/config.json /app/core/config/config.json
COPY --from=builder /src/core/config/license.json /app/core/config/license.json

# Update dynamic linker cache
RUN ldconfig

WORKDIR /app

# Usage: admin_server [configPath] [dbPath] [port]
# Models and DB are mounted at runtime via docker-compose volumes
EXPOSE 8080

CMD ["admin_server"]

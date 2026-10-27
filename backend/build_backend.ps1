param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$BackendDir = Split-Path -Parent $PSCommandPath
$BuildRoot  = "$BackendDir/build"
$GenDir     = "$BuildRoot/$Config/generators"
$BuildDir   = "$BuildRoot/$Config"

Write-Host "=== Building FaceAuthentication Backend ($Config) ===" -ForegroundColor Cyan

# ── 1. Conan ──
if (-not (Get-Command conan -ErrorAction SilentlyContinue)) {
    Write-Host "[1] Installing Conan..." -ForegroundColor Yellow
    pip install conan
}
Write-Host "[1] Conan: $((conan --version).Split()[-1])" -ForegroundColor Green

# ── 2. Export custom packages ──
Write-Host "[2] Exporting custom packages..." -ForegroundColor Yellow
@(
    "log-entry/1.0.0-4-83193b47"
    "log-factory/1.0.0-1-56934db0"
    "byte-track/1.0.0-2-71f99ef7"
    "tensorrt-runtime/test-0.1"
) | ForEach-Object {
    $name, $ver = $_ -split '/', 2
    conan export "$BackendDir/conan-custom/$_" --version $ver
}

# ── 3. Profile ──
Write-Host "[3] Detecting profile..." -ForegroundColor Yellow
conan profile detect --force 2>$null

# ── 4. Install deps ──
Write-Host "[4] Installing Conan dependencies (this takes a while)..." -ForegroundColor Yellow
$null = New-Item -ItemType Directory -Path $GenDir -Force
conan install . `
    --output-folder=$GenDir `
    --build=missing `
    -s build_type=$Config `
    -s compiler.cppstd=17 `
    -c tools.cmake.cmaketoolchain:generator=Ninja `
    -o "opencv/*:with_gtk=False" `
    -o "opencv/*:with_wayland=False" `
    -o "opencv/*:with_v4l=False" `
    -o "opencv/*:with_ffmpeg=True" `
    -o "opencv/*:with_tiff=False" `
    -o "opencv/*:with_webp=False" `
    -o "ffmpeg/*:with_freetype=False" `
    -o "ffmpeg/*:with_openh264=False" `
    -o "ffmpeg/*:with_libaom=False" `
    -o "ffmpeg/*:with_libdav1d=False" `
    -o "ffmpeg/*:with_pulse=False" `
    -o "ffmpeg/*:with_libiconv=False"
if ($LASTEXITCODE -ne 0) { throw "conan install failed" }
Write-Host "       Dependencies installed" -ForegroundColor Green

# ── 5. Build ──
Write-Host "[5] Building face_backend.dll..." -ForegroundColor Yellow
$null = New-Item -ItemType Directory -Path $BuildDir -Force

cmake -S $BackendDir -B $BuildDir -G Ninja `
    -DCMAKE_TOOLCHAIN_FILE="$GenDir/conan_toolchain.cmake" `
    -DCMAKE_BUILD_TYPE=$Config
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

cmake --build $BuildDir --target face_backend
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

$DllPath = "$BuildDir/face_backend.dll"
if (Test-Path $DllPath) {
    Write-Host "       Done: $DllPath" -ForegroundColor Green
} else {
    Write-Host "       Build completed but DLL not found at $DllPath" -ForegroundColor Yellow
}

Write-Host "=== Build complete! ===" -ForegroundColor Cyan

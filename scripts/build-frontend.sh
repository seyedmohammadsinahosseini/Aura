#!/usr/bin/env bash
# Build Flutter web locally for Docker deployment.
# Run this before: docker compose up --build
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
APP_DIR="$PROJECT_ROOT/frontend/flutter/attendance_console"

echo "==> Building Flutter web (attendance_console)..."
cd "$APP_DIR"

flutter pub get
flutter build web --release --target=lib/main.dart

echo "==> Flutter web build complete: $APP_DIR/build/web/"

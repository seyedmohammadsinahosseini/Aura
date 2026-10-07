#!/usr/bin/env bash
# Capture honest, reproducible visuals from a *running* instance of this backend.
#
#   ./scripts/capture_screenshots.sh [base-url] [out-dir] [--with-crops]
#
# Default: http://localhost:8080 -> docs/images/captures
#
# What it collects:
#   console.txt          version banner + endpoint probe results + current commit
#   health.json          GET /api/health
#   stats.json           GET /api/dashboard/stats
#   cameras.json         GET /api/cameras   (credentials are redacted server-side)
#   persons.json         GET /api/persons   (truncated to 40 lines on purpose)
#   modules.json         GET /api/modules
#   summary.json         GET /api/attendance/summary?date=<today>
#   unknown-faces.json   the review queue, with 512-float embeddings stripped out
#   <uid>.jpg            one live preview frame per camera (from the MJPEG endpoint)
#   routes.txt           the documented endpoint list, from docs/API.md
#
# What it refuses by default: face crops. Those are biometric data about real people, and
# committing them to a public repository is the one mistake that history does not let you undo.
# --with-crops is opt-in, writes into a separate subdir and labels every file it saves.
set -euo pipefail

BASE="${1:-http://localhost:8080}"
OUT="${2:-docs/images/captures}"
WITH_CROPS=0
for arg in "$@"; do
  [[ "$arg" == "--with-crops" ]] && WITH_CROPS=1
done

cd "$(dirname "${BASH_SOURCE[0]}")/.."
mkdir -p "$OUT"

have() { command -v "$1" >/dev/null 2>&1; }
get() { curl -fsS --max-time 10 "$1" 2>/dev/null || echo '[]'; }
pretty() { python3 -m json.tool 2>/dev/null || cat; }

if ! have curl; then
  echo "error: curl is required" >&2
  exit 1
fi
if ! have python3; then
  echo "error: python3 is required (used for JSON shaping)" >&2
  exit 1
fi

echo "== probing $BASE"
if ! curl -fsS --max-time 5 "$BASE/api/health" >/dev/null; then
  echo "error: $BASE/api/health is not reachable." >&2
  echo "       start the backend first:  ./build/faceauth   (or ./build/faceauth-admin)" >&2
  exit 1
fi

# --- console / metadata -------------------------------------------------------
{
  echo "# captured $(date -Is) from $BASE"
  echo
  if [[ -x build/faceauth ]]; then
    echo '$ ./build/faceauth --version'
    ./build/faceauth --version
  fi
  if [[ -x build/faceauth-admin ]]; then
    echo '$ ./build/faceauth-admin --version'
    ./build/faceauth-admin --version
  fi
  echo
  echo '$ curl -sI $BASE/api/health'
  curl -sI --max-time 5 "$BASE/api/health" | sed 's/^/  /' || true
  echo
  echo '$ git rev-parse --short HEAD'
  git rev-parse --short HEAD 2>/dev/null | sed 's/^/  /' || true
} >"$OUT/console.txt"

# --- JSON snapshots ----------------------------------------------------------
get "$BASE/api/health"          | pretty >"$OUT/health.json"
get "$BASE/api/modules"         | pretty >"$OUT/modules.json"
get "$BASE/api/cameras"         | pretty >"$OUT/cameras.json"
get "$BASE/api/dashboard/stats" | pretty >"$OUT/stats.json"
get "$BASE/api/persons"         | pretty | head -40 >"$OUT/persons.json"
get "$BASE/api/attendance/summary?date=$(date +%F)" | pretty >"$OUT/summary.json"

# The review queue embeds a 512-float vector per row: useful for the API, noise in a
# screenshot. Keep the metadata, drop the vectors.
get "$BASE/api/unknown-faces" >"$OUT/.unknown-faces.raw.json"
python3 - "$OUT" <<'PYTHON_SNIP'
import json
import pathlib
import sys

out = pathlib.Path(sys.argv[1])
raw = out / ".unknown-faces.raw.json"
try:
    rows = json.loads(raw.read_text())
except Exception:
    rows = []
if not isinstance(rows, list):
    rows = []
slim = [{k: v for k, v in row.items() if k != "embedding"} for row in rows if isinstance(row, dict)]
(out / "unknown-faces.json").write_text(json.dumps(slim, indent=2) + "\n")
raw.unlink(missing_ok=True)
PYTHON_SNIP

if [[ -f docs/API.md ]]; then
  grep -oE '^\| (GET|POST|PUT|DELETE) \| `[^`]+`' docs/API.md | sed 's/^| //' >"$OUT/routes.txt" || true
fi

# --- live preview frames -----------------------------------------------------
uids=$(python3 - "$OUT/cameras.json" <<'PYTHON_SNIP'
import json
import sys

try:
    data = json.load(open(sys.argv[1]))
except Exception:
    data = []
for cam in data if isinstance(data, list) else (data or {}).get("cameras", []):
    uid = cam.get("uid") or cam.get("cameraUid")
    if uid:
        print(uid)
PYTHON_SNIP
)

frames=0
if [[ -n "$uids" ]]; then
  if have ffmpeg; then
    while IFS= read -r uid; do
      [[ -z "$uid" ]] && continue
      safe=$(printf '%s' "$uid" | tr -c 'A-Za-z0-9._-' '_')
      if ffmpeg -y -loglevel error -i "$BASE/api/cameras/by-uid/$uid/stream" -frames:v 1 \
          "$OUT/preview-$safe.jpg" 2>/dev/null; then
        frames=$((frames + 1))
        echo "   preview-$safe.jpg"
      else
        echo "   (no frame from $uid - stream_enabled off, or camera offline)"
      fi
    done <<<"$uids"
  else
    echo "note: install ffmpeg to grab MJPEG frames automatically:" >&2
    echo "      ffmpeg -i \"$BASE/api/cameras/1/stream\" -frames:v 1 live-01.jpg" >&2
  fi
fi

# --- optional crops (explicit opt-in: biometric data) ------------------------
if [[ $WITH_CROPS -eq 1 ]]; then
  echo
  echo "!! --with-crops: writing face images of real people into $OUT/crops/"
  echo "!! Do not commit these. Delete the directory before you push, or add"
  echo "!! '$OUT/crops/' to .gitignore."
  mkdir -p "$OUT/crops"
  python3 - "$BASE" "$OUT" <<'PYTHON_SNIP'
import json
import pathlib
import sys
import urllib.parse
import urllib.request

base = sys.argv[1].rstrip("/")
out = pathlib.Path(sys.argv[2])
queue = out / "unknown-faces.json"
if not queue.is_file():
    print("   no unknown-faces.json, skipping crops")
    raise SystemExit(0)

count = 0
for row in json.loads(queue.read_text()):
    path = row.get("imagePath") or row.get("image_path")
    if not path:
        continue
    url = f"{base}/api/files/{urllib.parse.quote(str(path).lstrip('/'))}"
    try:
        with urllib.request.urlopen(url, timeout=10) as response:
            payload = response.read()
    except Exception as exc:
        print(f"   skip {path}: {exc}")
        continue
    target = out / "crops" / pathlib.Path(str(path)).name
    target.write_bytes(payload)
    count += 1
    print(f"   crops/{target.name}  ({len(payload)} bytes) <- REAL FACE IMAGE, do not commit")
print(f"   {count} crop(s) captured into {out}/crops")
PYTHON_SNIP
fi

echo
echo "== wrote $(find "$OUT" -type f | wc -l) files to $OUT ($frames preview frame(s))"
echo "   review before committing: this repo only tracks images under docs/images/*.{png,jpg}"

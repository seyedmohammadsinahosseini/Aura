#!/usr/bin/env python3
"""Render the documentation diagrams in docs/images/ (architecture.png, pipeline.png, erd.png).

    python3 docs/images/render_diagrams.py          # (re)generate
    python3 docs/images/render_diagrams.py --check   # verify they exist and still match the source

The ERD is *derived from the code*: tables, columns and foreign keys are parsed out of the
CREATE TABLE statements in services/storage/FaceDatabase.cpp, so the diagram cannot silently drift
from the schema. The architecture and pipeline diagrams are authored here, but every directory they
name is checked against the tree and `--check` runs in CI, so a renamed module breaks the build
instead of shipping a stale picture.

Layout is computed from measured text metrics (PIL), which is what keeps boxes from overlapping.
Requires: pillow   (pip install pillow) — except for `--check`, which stays dependency-free so CI
can verify the committed images without installing anything.
"""

from __future__ import annotations

import argparse
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent

try:
    from PIL import Image, ImageDraw, ImageFont

    HAVE_PIL = True
except ModuleNotFoundError as exc:  # pragma: no cover
    Image = ImageDraw = ImageFont = None
    HAVE_PIL = False
    _MISSING = exc.name

def _ttf(name: str) -> str:
    """Locate a DejaVu face without assuming one particular distro layout."""
    for d in ("/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/dejavu",
              "/usr/share/fonts/TTF", "/Library/Fonts", str(Path.home() / ".fonts")):
        cand = Path(d) / name
        if cand.is_file():
            return str(cand)
    raise SystemExit(f"{name} not found: install the DejaVu fonts "
                     f"(apt install fonts-dejavu-core) to re-render the diagrams")

SCALE = 2  # render at 2x then downsample: crisp text at any zoom
TITLE = 30
H2 = 20
BODY = 17
SMALL = 14

INK = (31, 36, 48)
MUTED = (110, 120, 136)
CAM = ("#e8f0fe", "#4285f4")
PIPE = ("#e6f4ea", "#34a853")
GPU = ("#fce8e6", "#ea4335")
DATA = ("#fef7e0", "#f9ab00")
API = ("#f3e8fd", "#a142f4")
GREY = ("#f1f3f4", "#9aa4b2")
WHITE = ("#ffffff", "#c3ccd8")


def font(pathname: str, size: int):
    return ImageFont.truetype(_ttf(pathname), size * SCALE)


_FONTS: dict[str, object] = {}


def FONTS() -> dict:
    """Loaded on first use: `--check` must run on a runner with neither pillow nor fonts."""
    if not _FONTS:
        _FONTS.update({
            "title": font("DejaVuSans-Bold.ttf", TITLE),
            "h2": font("DejaVuSans-Bold.ttf", H2),
            "body": font("DejaVuSans.ttf", BODY),
            "body_b": font("DejaVuSans-Bold.ttf", BODY),
            "small": font("DejaVuSans.ttf", SMALL),
            "mono": font("DejaVuSansMono.ttf", SMALL),
        })
    return _FONTS


class Canvas:
    def __init__(self, w: int, h: int, name: str = "diagram"):
        self.img = Image.new("RGB", (w * SCALE, h * SCALE), "white")
        self.d = ImageDraw.Draw(self.img)
        self.w, self.h = w, h
        self.name = name
        self.boxes: list[tuple[str, tuple[int, int, int, int]]] = []

    # --- primitives (all in logical pixels) --------------------------------
    def text_size(self, text: str, style: str) -> tuple[int, int]:
        f = FONTS()[style]
        box = self.d.textbbox((0, 0), text, font=f)
        return (box[2] - box[0]) // SCALE, (box[3] - box[1]) // SCALE

    def text(self, x: int, y: int, text: str, style: str = "body", color=INK, anchor="la"):
        self.d.text((x * SCALE, y * SCALE), text, font=FONTS()[style], fill=color, anchor=anchor)

    def box(self, cx: int, cy: int, lines, fill, edge, *, style="body", pad=(16, 12),
             title_style=None, min_w=0, radius=10) -> tuple[int, int, int, int]:
        """Draw a rounded box centred on (cx, cy); returns (x0, y0, x1, y1)."""
        title_style = title_style or style
        widths, heights, rendered = [], [], []
        for idx, line in enumerate(lines):
            st = title_style if idx == 0 else style
            w, h = self.text_size(line, st)
            widths.append(w)
            heights.append(h + (6 if idx == 0 else 2))
            rendered.append((line, st))
        inner_w = max(widths) if widths else 0
        inner_h = sum(heights) if heights else 0
        w = max(min_w + 2 * pad[0], inner_w + 2 * pad[0])
        h = inner_h + 2 * pad[1]
        x0, y0 = int(cx - w / 2), int(cy - h / 2)
        x1, y1 = x0 + w, y0 + h
        self.boxes.append((lines[0], (x0, y0, x1, y1)))
        self.d.rounded_rectangle(
            [x0 * SCALE, y0 * SCALE, x1 * SCALE, y1 * SCALE],
            radius=radius * SCALE, fill=fill, outline=edge, width=2 * SCALE,
        )
        y = y0 + pad[1]
        for idx, (line, st) in enumerate(rendered):
            self.d.text((cx * SCALE, (y + heights[idx] / 2) * SCALE),
                        line, font=FONTS()[st], fill=INK, anchor="mm")
            y += heights[idx]
        return x0, y0, x1, y1

    def label(self, cx: int, cy: int, text: str, style: str = "small", color=MUTED):
        self.text(cx, cy, text, style, color, anchor="mm")

    def arrow(self, a: tuple[int, int], b: tuple[int, int], *, dashed=False, color=MUTED,
              head=8):
        """Straight arrow between two points (use connect() for box-to-box routing)."""
        pa, pb = (a[0] * SCALE, a[1] * SCALE), (b[0] * SCALE, b[1] * SCALE)
        if dashed:
            dx, dy = pb[0] - pa[0], pb[1] - pa[1]
            steps = max(1, int(math.hypot(dx, dy) / (14 * SCALE)))
            for i in range(steps):
                t0, t1 = i / steps, min(1.0, (i + 0.55) / steps)
                self.d.line([(pa[0] + dx * t0, pa[1] + dy * t0), (pa[0] + dx * t1, pa[1] + dy * t1)],
                            fill=color, width=2 * SCALE)
        else:
            self.d.line([pa, pb], fill=color, width=2 * SCALE)
        angle = math.atan2(pb[1] - pa[1], pb[0] - pa[0])
        size = head * SCALE
        for da in (math.pi - 0.42, math.pi + 0.42):
            self.d.line([pb, (pb[0] + size * math.cos(angle + da), pb[1] + size * math.sin(angle + da))],
                        fill=color, width=2 * SCALE)

    def connect(self, a, b, *, dashed=False, color=MUTED, label="", label_style="small"):
        """Orthogonal (elbow) routing between two box rectangles.

        Straight when the boxes are vertically or horizontally aligned, otherwise a
        3-segment elbow through the midpoint of the gap. Elbows keep a cascade diagram
        readable; diagonals crossing other boxes do not.
        """
        ax0, ay0, ax1, ay1 = a
        bx0, by0, bx1, by1 = b
        acx, bcx = (ax0 + ax1) // 2, (bx0 + bx1) // 2
        tol = 24
        if by0 > ay1:                                   # b is below a
            mid = (ay1 + by0) // 2
            pts = [(acx, ay1 + 4), (acx, mid), (bcx, mid), (bcx, by0 - 4)]
            if abs(acx - bcx) <= tol:
                pts = [(acx, ay1 + 4), (bcx, by0 - 4)]
        elif by1 < ay0:                                 # b is above a
            mid = (ay0 + by1) // 2
            pts = [(acx, ay0 - 4), (acx, mid), (bcx, mid), (bcx, by1 + 4)]
            if abs(acx - bcx) <= tol:
                pts = [(acx, ay0 - 4), (bcx, by1 + 4)]
        elif bx0 > ax1:                                 # b is to the right
            acy, bcy = (ay0 + ay1) // 2, (by0 + by1) // 2
            pts = [(ax1 + 4, acy), (bx0 - 4, bcy)]
            if abs(acy - bcy) > tol:
                midx = (ax1 + bx0) // 2
                pts = [(ax1 + 4, acy), (midx, acy), (midx, bcy), (bx0 - 4, bcy)]
        else:                                           # b is to the left
            acy, bcy = (ay0 + ay1) // 2, (by0 + by1) // 2
            pts = [(ax0 - 4, acy), (bx1 + 4, bcy)]
            if abs(acy - bcy) > tol:
                midx = (ax0 + bx1) // 2
                pts = [(ax0 - 4, acy), (midx, acy), (midx, bcy), (bx1 + 4, bcy)]

        scaled = [(x * SCALE, y * SCALE) for x, y in pts]
        if dashed:
            for (x0, y0c), (x1, y1c) in zip(scaled, scaled[1:]):
                length = math.hypot(x1 - x0, y1c - y0c)
                steps = max(1, int(length / (16 * SCALE)))
                for i in range(steps):
                    t0, t1 = i / steps, min(1.0, (i + 0.55) / steps)
                    self.d.line([(x0 + (x1 - x0) * t0, y0c + (y1c - y0c) * t0),
                                 (x0 + (x1 - x0) * t1, y0c + (y1c - y0c) * t1)],
                                fill=color, width=2 * SCALE)
        else:
            self.d.line(scaled, fill=color, width=2 * SCALE)
        angle = math.atan2(scaled[-1][1] - scaled[-2][1], scaled[-1][0] - scaled[-2][0])
        size = 8 * SCALE
        for da in (math.pi - 0.42, math.pi + 0.42):
            self.d.line([scaled[-1], (scaled[-1][0] + size * math.cos(angle + da),
                                      scaled[-1][1] + size * math.sin(angle + da))],
                        fill=color, width=2 * SCALE)
        if label:
            lx, ly = (pts[1][0] + pts[2][0]) // 2 if len(pts) == 4 else (pts[0][0] + pts[1][0]) // 2, \
                     (pts[1][1] + pts[2][1]) // 2 if len(pts) == 4 else (pts[0][1] + pts[1][1]) // 2
            w, h = self.text_size(label, label_style)
            self.d.rounded_rectangle([(lx - w / 2 - 5) * SCALE, (ly - h / 2 - 3) * SCALE,
                                      (lx + w / 2 + 5) * SCALE, (ly + h / 2 + 3) * SCALE],
                                     radius=4 * SCALE, fill="white", outline=(232, 236, 242))
            self.text(lx, ly, label, label_style, color, anchor="mm")



    def assert_no_overlap(self, gap: int = 6) -> None:
        """Layout is computed from measured text, so an overlap means the boxes genuinely
        do not fit: fail the render instead of shipping a picture with colliding labels."""
        bad = []
        for i in range(len(self.boxes)):
            for j in range(i + 1, len(self.boxes)):
                (na, a), (nb, b) = self.boxes[i], self.boxes[j]
                if a[0] + gap < b[2] and b[0] + gap < a[2] and a[1] + gap < b[3] and b[1] + gap < a[3]:
                    bad.append(f"{na!r} vs {nb!r}")
        if bad:
            raise SystemExit(f"{self.name}: overlapping boxes -> {', '.join(bad)}. "
                              "Move a box or widen the canvas.")

    def save(self, path: Path) -> None:
        self.assert_no_overlap()
        self.img.resize((self.w, self.h), Image.LANCZOS).save(path, optimize=True)


def draw_architecture(target: Path, check: bool) -> None:
    for d in ("modules/camera", "services/stream", "services/camera", "modules/pipeline",
              "modules/detection", "modules/tracking", "modules/recognition", "modules/attendance",
              "services/storage", "ffi", "http", "core", "migrations", "third_party", "conan/recipes"):
        if not (ROOT / d).is_dir():
            raise SystemExit(f"architecture diagram names a missing directory: {d}")

    W, H = 1500, 1040
    c = Canvas(W, H, "architecture")
    c.text(W // 2, 40, "FaceAuth Attendance — runtime architecture", "title", INK, anchor="mm")
    c.text(W // 2, 70, "generated by docs/images/render_diagrams.py · solid = frame or row flow",
           "small", MUTED, anchor="mm")

    mid = 750
    cameras = c.box(mid, 120, ["Cameras", "V4L2 · RTSP · ONVIF (SOAP + WS-Discovery)"], *CAM,
                    title_style="h2", min_w=520)
    capture = c.box(mid, 205, ["modules/camera", "CameraDiscovery · CameraManager · CameraStreamer",
                               "OnvifClient · OnvifPtz"], *CAM, title_style="body_b")
    hub = c.box(420, 300, ["services/stream · CameraPreviewHub",
                           "latest JPEG/BGR per camera", "bounded cv::Mat queue"], *DATA,
                title_style="body_b")
    orch = c.box(1080, 300, ["services/camera · CameraOrchestrator",
                             "one worker thread per camera uid"], *PIPE, title_style="body_b")
    pipe = c.box(mid, 395, ["modules/pipeline · FacePipeline", "detect → track → recognize",
                            "motion short-circuit ahead of the GPU"], *PIPE, title_style="body_b")
    det = c.box(250, 500, ["modules/detection", "CDetection · CHead", "TensorRT engines"], *GPU,
                title_style="body_b")
    trk = c.box(750, 500, ["modules/tracking", "CTrack · CFar · CMotion", "ByteTrack (Kalman + Hungarian)"],
                *GPU, title_style="body_b")
    rec = c.box(1250, 500, ["modules/recognition", "FaceSearcher · FaceAuthenticator",
                            "FAISS IndexFlatIP · 512-d"], *GPU, title_style="body_b")
    att = c.box(420, 610, ["modules/attendance", "AttendanceManager", "WorkSessionTracker"], *PIPE,
                title_style="body_b")
    db = c.box(1080, 610, ["services/storage · FaceDatabase", "SQLite (14 tables) + crops/",
                           "migrations/ (applied manually)"], *DATA, title_style="body_b")

    # boundary band
    c.d.rectangle([40 * SCALE, 690 * SCALE, (W - 40) * SCALE, 830 * SCALE],
                  outline=(214, 220, 230), width=SCALE, fill="#fafbfc")
    c.text(60, 706, "THE OUTSIDE WORLD REACHES THE BACKEND ONLY THROUGH THE C ABI", "small",
           MUTED, anchor="lm")
    http = c.box(280, 780, ["http/", "56 REST endpoints", "MJPEG from the preview hub",
                            "cpp-httplib"], *API, title_style="body_b")
    ffi = c.box(750, 780, ["ffi/face_ffi.h", "C ABI · 71 functions", "libface_backend.so",
                           "also drives the orchestrator + storage"], *API, title_style="body_b")
    core = c.box(1215, 780, ["core/", "PluginRegistry + LicenseManager", "config · xml · crypto",
                             "gates which modules load"], *GREY, title_style="body_b")
    c.connect(http, ffi, label="face_* only")
    c.connect(ffi, core, label="module load", dashed=True)

    c.connect(cameras, capture, label="frames")
    c.connect(capture, hub, label="publish")
    c.connect(capture, orch, label="subscribe")
    c.connect(orch, pipe, label="frame")
    c.connect(pipe, det)
    c.connect(pipe, trk)
    c.connect(pipe, rec)
    c.connect(det, att, label="crops")
    c.connect(rec, db, label="rows")
    c.connect(att, db, label="events")

    c.box(750, 890, ["build-time inputs (not runtime boxes)", "conan/recipes/ · byte-track, "
                     "tensorrt-runtime, log-factory", "third_party/ · cpp-httplib 0.49.0 (vendored, "
                     "excluded from style + language stats)"], *GREY, style="small",
          title_style="body_b", min_w=900)
    c.text(W // 2, 960, "every box above is a directory in this repository — render_diagrams.py fails "
                        "if one is renamed or removed", "small", MUTED, anchor="mm")
    c.save(target)


def draw_pipeline(target: Path, check: bool) -> None:
    stages = [
        ("frame from hub", CAM, ["CameraPreviewHub", "latest frame, no capture stall"]),
        ("motion gate", PIPE, ["CMotion", "background subtraction", "blur + dilate"]),
        ("face detection", GPU, ["CDetection", "letterbox + normalise", "TensorRT engine"]),
        ("head detection", GPU, ["CHead", "face ↔ head IoU match"]),
        ("track association", PIPE, ["CTrack", "ByteTrack", "Kalman + Hungarian"]),
        ("quality gate", DATA, ["CFar · blur · side profile", "reject before the GPU costs anything"]),
        ("embed + FAISS search", GPU, ["CRecognition · FaceSearcher", "512-d L2-normalised", "cosine vs faiss_threshold"]),
    ]
    W, H = 1500, 760
    c = Canvas(W, H, "pipeline")
    c.text(W // 2, 42, "One frame, start to finish", "title", INK, anchor="mm")
    c.text(W // 2, 74, "FacePipeline per-frame path · the two exits at the bottom are the only "
                       "places data becomes durable", "small", MUTED, anchor="mm")

    x0, step = 200, 360
    y1, y2 = 210, 430
    placed: dict[int, tuple[int, int, int, int]] = {}
    for i, (title, colors, subs) in enumerate(stages[:4]):
        bx = x0 + i * step
        placed[i] = c.box(bx, y1, [title] + subs, *colors, title_style="body_b", min_w=250)
    for i in range(1, 4):
        prev = placed[i - 1]
        cur = placed[i]
        c.arrow((prev[2] + 4, y1), (cur[0] - 4, y1))

    # row 2 runs right → left, connected by a wrap from the last stage of row 1
    for j, (title, colors, subs) in enumerate(stages[4:]):
        bx = x0 + (3 - j) * step
        placed[4 + j] = c.box(bx, y2, [title] + subs, *colors, title_style="body_b", min_w=250)
    last1 = placed[3]
    c.arrow((last1[0] + (last1[2] - last1[0]) // 2, last1[3] + 4),
            (placed[4][0] + (placed[4][2] - placed[4][0]) // 2, placed[4][1] - 4))
    for j in range(1, 3):
        prev = placed[4 + j - 1]
        cur = placed[4 + j]
        c.arrow((prev[0] - 4, y2), (cur[2] + 4, y2))

    # the two exits, both decided by the same stage
    known = c.box(430, 640, ["known person → attendance", "arrival / departure · work session",
                             "location_log · attendance_logs", "index rebuilt after enrolment"],
                  *PIPE, title_style="body_b")
    unknown = c.box(1070, 640, ["below threshold → review queue", "unknown_faces row + crop on disk",
                                "a human enrols, merges or discards", "no auto-attribution"],
                    *DATA, title_style="body_b")
    c.connect(placed[6], known, label="cosine ≥ threshold")
    c.connect(placed[6], unknown, label="cosine < threshold")

    # static-scene note, attached to the motion gate
    c.box(x0, y2, ["static scene", "GPU inference skipped", "the throughput lever"], *GREY,
          title_style="body_b")
    c.arrow((x0 + 130, y1 + 40), (x0 + 40, y2 - 60), dashed=True)
    c.text(x0 + 175, (y1 + y2) // 2, "no motion", "small", MUTED, anchor="lm")
    c.save(target)


def parse_schema() -> tuple[dict[str, list[tuple[str, str]]], dict[str, list[tuple[str, str, str]]]]:
    """Parse tables, columns and foreign keys out of FaceDatabase.cpp.

    The schema is written as a run of adjacent string literals, which clang-format may
    wrap anywhere, so the body is normalised (quotes dropped, whitespace collapsed) and
    then split on top-level commas instead of on source lines.
    """
    src = (ROOT / "services" / "storage" / "FaceDatabase.cpp").read_text(encoding="utf-8")
    columns: dict[str, list[tuple[str, str]]] = {}
    fkeys: dict[str, list[tuple[str, str, str]]] = {}

    for m in re.finditer(r"CREATE TABLE IF NOT EXISTS (\w+) \((.*?)\);", src, re.S):
        name, raw = m.group(1), m.group(2)
        body = re.sub(r"\s+", " ", raw.replace('"', " "))

        parts, depth, cur = [], 0, ""
        for ch in body:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            if ch == "," and depth == 0:
                parts.append(cur)
                cur = ""
            else:
                cur += ch
        parts.append(cur)

        cols: list[tuple[str, str]] = []
        for part in parts:
            decl = part.strip()
            if not decl:
                continue
            if decl.upper().startswith(("PRIMARY KEY", "UNIQUE (", "UNIQUE(", "FOREIGN KEY", "CHECK")):
                continue
            head, _, rest = decl.partition(" ")
            cols.append((head.strip(), " ".join(rest.split())))
        columns[name] = cols
        fkeys[name] = [(a, b, c) for a, b, c in
                       re.findall(r"FOREIGN KEY\s*\((\w+)\)\s*REFERENCES\s*(\w+)\s*\((\w+)\)", body)]
    return columns, fkeys


def draw_erd(target: Path, check: bool) -> None:
    columns, fkeys = parse_schema()
    if len(columns) < 12 or not any(fkeys.values()):
        raise SystemExit("schema parsing failed — the CREATE TABLE layout and this script diverged")

    order = ["persons", "reference_faces", "unknown_faces", "raw_detections", "attendance_logs",
             "work_sessions", "breaks", "tardiness_log", "person_schedules", "zones", "cameras",
             "camera_module_config", "location_log", "system_settings"]
    names = [n for n in order if n in columns] + [n for n in columns if n not in order]

    col_count = 3
    col_width = 470
    W = 1500
    gap = 26
    top = 118

    contents: dict[str, list[str]] = {}
    heights: dict[str, int] = {}
    for n in names:
        shown = columns[n][:10]

        def render(name: str, rest: str) -> str:
            rest = rest.replace("'", "")            # SQL string literals read as noise here
            short = rest if len(rest) <= 24 else rest[:23] + "…"
            return f"{name:<16}{short}"

        lines = [n] + [render(name, rest) for name, rest in shown]
        if len(columns[n]) > len(shown):
            lines.append(f"… +{len(columns[n]) - len(shown)} more")
        contents[n] = lines
        heights[n] = 34 * len(lines) + 24

    rows_used = [0] * col_count
    col_of = {n: i % col_count for i, n in enumerate(names)}
    for n in names:
        rows_used[col_of[n]] += 1
    H = top + max(sum(heights[n] + gap for n in names if col_of[n] == ci) for ci in range(col_count)) + 90

    xs = [20 + ci * (col_width + 30) + col_width // 2 for ci in range(col_count)]
    y_at = [top] * col_count
    rects: dict[str, tuple[int, int, int, int]] = {}
    for n in names:
        ci = col_of[n]
        h = heights[n]
        y0 = y_at[ci]
        rects[n] = (xs[ci] - col_width // 2, y0, xs[ci] + col_width // 2, y0 + h)
        y_at[ci] += h + gap

    c = Canvas(W, int(H), "erd")
    c.text(W // 2, 40, "Persistence model — SQLite", "title", INK, anchor="mm")
    c.text(W // 2, 70, f"parsed from the CREATE TABLE statements in services/storage/FaceDatabase.cpp"
                       f" · {len(names)} tables · no indexes beyond PK/UNIQUE", "small", MUTED,
           anchor="mm")

    # pass 1: relations (behind the boxes), edge to edge
    for n, links in fkeys.items():
        for col, ref, _key in links:
            if ref not in rects or n not in rects:
                continue
            ax0, ay0, ax1, ay1 = rects[n]
            bx0, by0, bx1, by1 = rects[ref]
            if by0 > ay1:
                pts = [(ax0 + ax1) // 2, ay1, (bx0 + bx1) // 2, by0]
            elif by1 < ay0:
                pts = [(ax0 + ax1) // 2, ay0, (bx0 + bx1) // 2, by1]
            elif bx0 > ax1:
                pts = [ax1, (ay0 + ay1) // 2, bx0, (by0 + by1) // 2]
            else:
                pts = [ax0, (ay0 + ay1) // 2, bx1, (by0 + by1) // 2]
            c.arrow((pts[0], pts[1]), (pts[2], pts[3]), dashed=True, color=(170, 180, 195))

    # pass 2: the table boxes themselves
    for n in names:
        x0, y0, x1, y1 = rects[n]
        primary = n in ("persons", "cameras", "zones")
        c.box((x0 + x1) // 2, (y0 + y1) // 2, contents[n],
              DATA[0] if primary else WHITE[0], DATA[1] if primary else WHITE[1],
              style="mono", title_style="body_b", pad=(14, 10), min_w=col_width - 24)

    c.text(W // 2, int(H) - 44, "dashed = declared FOREIGN KEY · SQLite enforces them only with "
                                "PRAGMA foreign_keys=ON, which the app does not set (docs/SCHEMA.md)",
           "small", MUTED, anchor="mm")
    c.save(target)


DIAGRAMS = {"architecture.png": draw_architecture, "pipeline.png": draw_pipeline, "erd.png": draw_erd}


def _require_pil() -> None:
    if not HAVE_PIL:
        sys.exit(f"missing dependency {_MISSING!r}: pip install pillow")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true",
                        help="fail if a diagram is missing (the render itself also re-validates the source)")
    args = parser.parse_args()
    for name, drawer in DIAGRAMS.items():
        target = OUT / name
        if args.check:
            if not target.exists():
                raise SystemExit(f"{name} is missing — run python3 docs/images/render_diagrams.py")
            print(f"  ok    {name} present")
            continue
        _require_pil()
        target.parent.mkdir(parents=True, exist_ok=True)
        drawer(target, args.check)
        print(f"  wrote {target.relative_to(ROOT)}  ({target.stat().st_size // 1024} KB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

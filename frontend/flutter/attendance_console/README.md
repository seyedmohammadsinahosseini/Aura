# FaceAuthentication Console

Flutter web admin panel for the FaceAuthentication C++/TensorRT backend.
A clean-room rewrite of the previous `attendance_app` web UI — web-only (REST),
no FFI, no desktop mode.

## Stack

| Concern | Choice |
|---|---|
| UI | Material 3, Vazirmatn typeface, neutral palette + indigo accent |
| State / DI | Riverpod 3 (no codegen) |
| Routing | go_router (URL per page, deep links, `StatefulShellRoute`) |
| Charts | fl_chart |
| Calendar | Jalali (فارسی) via `shamsi_date`, Gregorian for English |
| HTTP | `package:http` against the backend REST API |

## Pages

- **Dashboard** — today's KPIs, 7-day attendance chart, camera status, today board
- **Live** — polled MJPEG-style previews (subscribe → poll → unsubscribe), fullscreen
- **People** — directory + profile (day timeline, breaks, schedule editor, last seen, photo upload)
- **Attendance** — daily board for any date
- **Cameras** — discovery scan, manual add/edit, connection test, stream/AI toggles
- **Zones** — access-level management
- **Unknown faces** — enroll / merge / discard review queue
- **Reports** — weekly hours per person, team tardiness
- **Settings** — server URL, theme (light/dark/system), language (fa/en)

## Run (dev)

```bash
flutter pub get
flutter run -d chrome            # talks to http://localhost:8080 by default
```

## Build

```bash
flutter build web --release       # → build/web/
```

## Deploy

Deployed through the repo's docker-compose (nginx serving `build/web` with a
`/api/` proxy to the backend):

```bash
./scripts/build-frontend.sh       # builds this app
docker compose up --build         # frontend on :3000, backend on :8080
```

## Notes

- The backend publishes live preview frames **only while subscribed**; the
  `PreviewPoller` subscribes before polling and unsubscribes on dispose.
- Dates sent to the API are Gregorian `YYYY-MM-DD`; display is Jalali for fa.
- The app is locale-aware RTL (Persian) / LTR (English) with a single string
  table in `lib/core/app_strings.dart`.

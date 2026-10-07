# Security policy

**Scope:** this repository contains a *reference/learning* implementation of a LAN-only vision
backend. It ships without authentication and without TLS, and it says so everywhere rather than
hoping nobody notices. Read this before you run it, and read
[docs/SECURITY_AUDIT.md](docs/SECURITY_AUDIT.md) for what I found when I reviewed my own code.

## Supported versions

| Version | Supported |
|---------|-----------|
| `main` (1.0.0 line) | :white_check_mark: community fixes, best-effort |
| anything older | :x: not supported — rebase onto `main` first |

## The one rule that matters

> **Never expose port 8080 beyond a trusted network segment.**

`http/http_server.cpp` binds `0.0.0.0:8080`, sends `Access-Control-Allow-Origin: *`, and performs
**no authentication or authorisation** on any endpoint. Anyone who can reach the port can:

- read the full person list, attendance history, timelines and location log;
- read every stored face embedding (`GET /api/embeddings/references`);
- enrol, rename and **delete** people, and delete cameras and zones;
- add a camera (i.e. point the backend at an RTSP source of the attacker's choosing) and start
  inference on it;
- read any image inside the configured crops directory.

That is not a hypothetical list — those are the endpoints in [docs/API.md](docs/API.md), and there is
no auth layer between them and the socket.

## Hardening checklist for anything real

1. Bind to loopback and front it with a reverse proxy that terminates TLS and authenticates:
   ```nginx
   location /api/ {
       allow 10.0.0.0/24; deny all;
       proxy_pass http://127.0.0.1:8080;
       proxy_set_header Authorization $http_authorization;
   }
   ```
2. Require a token in the handlers. The seam already exists: `face_last_error`-style helpers live in
   the FFI, and every handler goes through a single `registerRoutes()` — a pre-routing handler that
   checks `Authorization` (the `set_pre_routing_handler` slot is already used for CORS) is a ~30-line
   change and the first roadmap item.
3. Store camera credentials in a secret store or an encrypted column. Today `cameras.password` is
   plaintext in SQLite; mitigations in place are that the API never echoes it back and that no
   credential is ever committed to git.
4. Give the process what it needs and nothing more: dedicated user, `NoNewPrivileges=`,
   `ProtectSystem=strict`, `PrivateTmp=`, `DeviceAllow=/dev/video* rw` if V4L2 is used
   (see the systemd example in [docs/BUILDING.md](docs/BUILDING.md)).
5. Keep `crops/` and `attendance.db` on an encrypted volume with `0600`/`0700` permissions; they are
   the crown jewels (biometric templates + images).
6. Apply retention limits — see [docs/PRIVACY.md](docs/PRIVACY.md#4-what-a-real-deployment-must-add).

## What is already handled

| Area | Mitigation in the code |
|------|------------------------|
| Path traversal in `GET /api/files/{path}` | Canonicalised with `std::filesystem::weakly_canonical` against the configured crops directory; escapes → `403`, misses → `404`. |
| Credential leakage through the API/FFI | Camera reads return `hasPassword`; `password` is write-only. |
| Secrets in the repository | No credentials/IPs/hostnames in source; `config.json`, `license.json`, `*.db`, `crops/*`, images and model weights are git-ignored, and CI (`scripts/check_repo_hygiene.py`) fails if one appears. |
| ONVIF authentication | WS-Security `PasswordDigest` (`SHA1(nonce+created+password)` → Base64) per the ONVIF profile, credentials supplied at runtime by the operator, never default/guessed. |
| Binary hardening | `-fPIC`, `-Wl,-z,noexecstack` on the shared object; `-Werror=return-type`. |
| SQL injection | All queries are prepared statements with bound parameters; the only string-concatenated SQL is `PRAGMA table_info(<table>)` with table names from code. |
| Exceptions across the C ABI | Every `extern "C"` entry point catches and reports through `face_last_error()`. |
| Buffer sizing | Embedding length is validated at both the FFI and DB layers (`512 × float32`). |

## Known gaps, stated plainly

- **No authentication, no authorisation, no TLS, no rate limiting, no audit log of API writes.**
- **`license.json` is not signed in a verifiable way.** `signature` is parsed and ignored, so the
  entitlement gate cannot be trusted as a boundary; `max_cameras` is applied where workers start.
- **Plaintext credential storage** in `cameras.password`.
- **SHA-1 for ONVIF digests** — mandated by the ONVIF/WS-Security profile; documented in
  `core/crypto/Sha1.h`, and it is not used for anything else.
- **MJPEG streams occupy a handler thread for the whole connection**, and the server uses
  cpp-httplib's default pool size, `max(8, hardware_concurrency - 1)`
  (`CPPHTTPLIB_THREAD_POOL_COUNT`). A handful of persistent viewers can therefore starve the REST
  API — an availability issue, not
  a data one. `set_payload_max_size` and an explicit `ThreadPool(n)` are the levers, plus serving
  previews from a dedicated socket.
- **A few handlers parse JSON without validating required fields**; malformed bodies turn into `500`s
  (cpp-httplib catches) rather than clean `400`s.
- **`GET /api/unknown-faces` returns full embeddings** for every queued face; a leaked database plus
  that endpoint is the worst-case data exposure here.

Each gap above has a corresponding entry in the roadmap; none of them are hidden.

## Reporting a vulnerability

Open a **private** report via GitHub Security Advisories on the repository
(“Report a vulnerability”), or e-mail through the contact in the README. Please do not open a public
issue for anything that leaks data or bypasses access control. I aim to acknowledge within 7 days and
to publish a fix or a documented mitigation within 30; since this is a single-maintainer portfolio
project, "fix" may honestly mean "here is why you must not expose this, and here is the patch you
should apply locally".

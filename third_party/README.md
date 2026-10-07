# Vendored third-party code

This directory holds dependencies that are committed into the repository
instead of being fetched by the package manager. Everything here is
third-party code under its own license and is **excluded from formatting,
linting and the "first-party code" language statistics** (see
`.gitattributes` and `.clang-format`).

| Path | Upstream | License | How it is consumed |
|------|----------|---------|--------------------|
| `cpp-httplib/include/httplib.h` | [cpp-httplib 0.49.0](https://github.com/yhirose/cpp-httplib) | MIT | Header-only; `http/http_server.cpp` includes `"httplib.h"`. The include directory is added to the `face_backend` target in the top-level `CMakeLists.txt`. |

Do not edit vendored files: upgrade them by replacing the file with a new
upstream release and updating the table above (the version macro
`CPPHTTPLIB_VERSION` in the header records the pinned release).

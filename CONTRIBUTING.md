# Contributing

Thanks for taking an interest. This is a single-maintainer project published as a portfolio piece, so
the bar is "keep it reviewable" rather than "match a corporate process".

## Before you start

```bash
git clone https://github.com/seyedmohammadsinahosseini/cpp-face-auth-attendance.git
cd cpp-face-auth-attendance
cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests --output-on-failure
python3 scripts/check_repo_hygiene.py
./scripts/format.sh --check
```

Those three commands are exactly what CI runs, and none of them needs a GPU. If you want to build the
whole backend, follow [docs/BUILDING.md](docs/BUILDING.md) — you will need CUDA, TensorRT and the
non-distributed `log-entry` package.

## Ground rules

1. **One concern per pull request.** A refactor and a behaviour change do not mix well in review.
2. **Update the docs in the same commit.** `docs/API.md` is the contract for the UI; if an endpoint
   changes, its row changes with it. If you fix something the README claimed wrongly, fix the claim.
3. **No new dependencies without a reason.** Every Conan requirement is a build time you pay and a
   license you inherit. If you need a library for 20 lines of code, write the 20 lines.
4. **Never commit data.** Face crops, `*.db`, model weights and `core/config/config.json` are
   `.gitignore`d for a reason. `scripts/check_repo_hygiene.py` will fail your push; do not work around
   it by editing the ignore rules.
5. **Do not "fix" formatting in files you are not touching.** Use the pinned
   `clang-format==23.1.3` (`pip install clang-format==23.1.3`), which is what CI installs; a newer
   release reflows unrelated lines. The tree is clang-format clean with the
   pinned version; unrelated whitespace churn hides the real diff.

## Style

- C++20, 4-space indent, 100 columns, `snake_case` for functions/variables and `PascalCase` for types —
  matching what the existing code does rather than a fresh convention.
- `#include` order is meaningful here, so `SortIncludes: Never` is set on purpose. Do not re-sort
  includes in a formatting pass.
- Never let a C++ exception cross `ffi/face_ffi.*`: catch, `setError(...)`, return a negative code.
- Anything that owns a resource owns it with RAII (there is exactly one legacy `cudaMalloc` question
  left in the vendored `tensorrt-runtime` recipe — see `docs/SECURITY_AUDIT.md` 6.5 — and it needs a
  GPU to settle).
- Prefer returning `std::optional` / status codes to `bool + out-parameter` in new code, but keep the
  C ABI's `0 = success` convention at the boundary.

## Tests

The rule of thumb: **a change to logic that can run without a GPU must come with a test in `tests/`.**

- Add `tests/unit/test_<topic>.cpp` — plain `main()` with `check(condition, name)` calls, no framework
  dependency, exit code 0/1. That keeps the suite buildable on any machine.
- Register it in `tests/CMakeLists.txt` with `add_executable` + `add_test`.
- Anything that needs OpenCV/TensorRT should be exercised through the capture scripts and a real run
  instead; say in the PR description which hardware and which cameras (or files) you used.

Good first tests to write (all of these target code that already runs without a GPU):

| Target | What to assert |
|--------|----------------|
| `core/xml/XmlParser.cpp` | namespace-aware attribute lookup, missing-node handling, malformed input → no crash. |
| `core/license/LicenseManager.cpp` | expired vs. valid `expires`, empty `modules` → invalid, malformed JSON → error message set. |
| `modules/recognition/Utils.h` | the crop-directory helper and any pure math in that header. |
| `core/config/Config.cpp` | defaults when keys are absent; that `modules` overrides win over the top-level values; path resolution against `core/config`. |
| `services/storage/FaceDatabase.cpp` | schema creation and the idempotent column patches on a temp file, `validateEmbeddingSize` rejection, `deletePerson` cascade (which is currently incomplete — see 5.1). |

## Commit messages

Conventional Commits, imperative, and explain the *why* in the body:

```
fix(http): stop echoing camera passwords on camera reads

Credentials are only ever needed by the streamer, which reads them from SQLite
directly. Returning them from the API turned a LAN-only exposure into a full
credential leak, so the FFI serialiser now reports hasPassword instead.

Refs docs/SECURITY_AUDIT.md 1.2
```

## Security issues

Read [SECURITY.md](SECURITY.md) first. For anything that leaks data or bypasses a control, open a
private report (GitHub → Security → "Report a vulnerability") instead of a public issue.

## Pull-request checklist

- [ ] `ctest` and `python3 scripts/check_repo_hygiene.py` pass
- [ ] `./scripts/format.sh` produced no diff
- [ ] docs updated (`API.md` / `CONFIGURATION.md` / `CHANGELOG.md`)
- [ ] no real faces, databases, credentials or model weights in the diff
- [ ] if behaviour changed: what the old behaviour was, and why the new one is right

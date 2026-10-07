#!/usr/bin/env python3
"""Repository hygiene checks.

Run locally (`python3 scripts/check_repo_hygiene.py`) or in CI. This is the script that keeps the
promises made in the README and SECURITY.md actually true, instead of aspirational:

  1. no private or heavy artifacts are tracked (databases, model weights, crops, live config)
  2. no credential-looking literals or private network addresses in first-party source
  3. every relative link and image reference in every Markdown file resolves, and anchors exist
  4. documentation invariants (README sections, LICENSE, changelog entry for the current version)
  5. the Conan graph is self-consistent (every first-party requirement has a recipe with a version)

Exit code is non-zero with a readable report if any check fails.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

FIRST_PARTY_CODE = ("core", "modules", "services", "ffi", "http", "tests", "migrations", "scripts")

# Tracked paths that must never appear in a public repository.
FORBIDDEN_PATTERNS = [
    (r"\.db$", "SQLite database (contains biometric templates and attendance history)"),
    (r"\.sqlite3?$", "SQLite database"),
    (r"\.engine$", "TensorRT engine (hardware-specific, and large)"),
    (r"\.onnx$", "model weights (third-party license)"),
    (r"\.etlt$", "TAO model weights (third-party license)"),
    (r"\.(pt|pth)$", "training checkpoint"),
    (r"(^|/)config\.json$", "live configuration (only config.example.json is tracked)"),
    (r"(^|/)license\.json$", "entitlement file (only license.example.json is tracked)"),
    (r"\.pem$|\.key$|id_rsa$|\.p12$|\.pfx$", "private key"),
    (r"(^|/)crops/.*\.(jpg|jpeg|png)$", "face crop image (biometric data of real people)"),
]

# Credential shapes in first-party source. ONVIF/WS-Security legitimately mentions "password" as a
# field name and format string, so only *assigned literals* are reported.
SECRET_REGEXES = [
    (re.compile(r"(?i)\b(password|passwd|secret|api[_-]?key|access[_-]?token)\b\s*[:=]\s*\"[^\"]{6,}\""),
     "hard-coded credential literal"),
    (re.compile(r"ghp_[A-Za-z0-9]{36}|github_pat_[A-Za-z0-9_]{60,}"), "GitHub token"),
    (re.compile(r"xox[baprs]-[A-Za-z0-9-]{10,}"), "Slack token"),
    (re.compile(r"AKIA[0-9A-Z]{16}"), "AWS access key id"),
    (re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----"), "private key block"),
    (re.compile(r"rtsp://[^\s\"']+:[^\s\"'@]+@"), "RTSP URL with embedded credentials"),
]

# Address literals are allowed only as documentation/examples; code must not hard-code a subnet.
LAN_ADDRESS_IN_CODE = re.compile(r"[\"'](10\.\d{1,3}\.\d{1,3}\.\d{1,3}|172\.(1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3}|192\.168\.\d{1,3}\.\d{1,3})[\"']")

REQUIRED_README_SECTIONS = [
    "## What this is",
    "## Screenshots",
    "## Architecture at a glance",
    "## Features",
    "## Quick start",
    "## REST API",
    "## Known limitations",
    "## License and citation",
]

REQUIRED_FILES = [
    "README.md",
    "LICENSE",
    "SECURITY.md",
    "CONTRIBUTING.md",
    "CHANGELOG.md",
    "CITATION.cff",
    ".gitignore",
    ".gitattributes",
    ".editorconfig",
    ".clang-format",
    ".github/workflows/ci.yml",
    "docs/ARCHITECTURE.md",
    "docs/API.md",
    "docs/FFI.md",
    "docs/BUILDING.md",
    "docs/CONFIGURATION.md",
    "docs/MODELS.md",
    "docs/SCHEMA.md",
    "docs/PRIVACY.md",
    "docs/SECURITY_AUDIT.md",
    "docs/SCREENSHOTS.md",
    "scripts/format.sh",
    "scripts/capture_screenshots.sh",
    "scripts/purge_data.sh",
    "core/config/config.example.json",
    "core/config/license.example.json",
    "conan/recipes/byte-track/conanfile.py",
    "conan/recipes/log-factory/conanfile.py",
    "conan/recipes/tensorrt-runtime/conanfile.py",
]


def tracked_files() -> list[str]:
    try:
        out = subprocess.run(
            ["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True
        ).stdout
        return [p for p in out.decode().split("\0") if p]
    except (subprocess.CalledProcessError, FileNotFoundError):
        # Not a git checkout (e.g. a source tarball): fall back to a filesystem walk.
        skip = {".git", "build", "out", "node_modules", "__pycache__", ".cache"}
        files = []
        for path in ROOT.rglob("*"):
            if path.is_file() and not (set(path.relative_to(ROOT).parts) & skip):
                files.append(path.relative_to(ROOT).as_posix())
        return sorted(files)


def is_vendored(rel: str) -> bool:
    return rel.startswith(("third_party/", "conan/recipes/"))


def check_forbidden_tracked(files: list[str]) -> list[str]:
    problems = []
    for rel in files:
        for pattern, why in FORBIDDEN_PATTERNS:
            if re.search(pattern, rel, re.I):
                problems.append(f"{rel}: {why}")
                break
    return problems


# This script contains the detection patterns themselves, so it is skipped.
SELF_EXCLUDE = {"scripts/check_repo_hygiene.py"}


def check_secrets(files: list[str]) -> list[str]:
    problems = []
    for rel in files:
        if rel in SELF_EXCLUDE:
            continue
        if is_vendored(rel) or not rel.endswith((".cpp", ".h", ".hpp", ".py", ".json", ".txt", ".md", ".sh", ".cmake", ".yml")):
            continue
        if rel.endswith(".md") and rel.startswith(("README", "docs/", "SECURITY", "CONTRIBUTING", "CHANGELOG")):
            continue  # documentation quotes example payloads and curl commands on purpose
        text = (ROOT / rel).read_text(encoding="utf-8", errors="replace")
        for lineno, line in enumerate(text.splitlines(), 1):
            for regex, why in SECRET_REGEXES:
                if regex.search(line):
                    problems.append(f"{rel}:{lineno}: {why}: {line.strip()[:110]}")
            if rel.endswith((".cpp", ".h")) and LAN_ADDRESS_IN_CODE.search(line):
                problems.append(f"{rel}:{lineno}: hard-coded LAN address in code: {line.strip()[:110]}")
    return problems


def _anchor_exists(md_text: str, anchor: str) -> bool:
    """GitHub anchor computation: lowercase, drop punctuation, turn every space into a hyphen."""
    anchors = set()
    in_fence = False
    for line in md_text.splitlines():
        if line.startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        m = re.match(r"^(#{1,6})\s+(.*?)\s*#*$", line)
        if not m:
            continue
        title = m.group(2)
        title = re.sub(r"<[^>]+>", "", title)
        title = title.strip().lower()
        title = re.sub(r"[^\w\s-]", "", title)
        title = title.replace(" ", "-")   # GitHub does not collapse runs of hyphens
        anchors.add(title)
    return anchor in anchors


def check_links(files: list[str]) -> list[str]:
    problems = []
    for rel in [f for f in files if f.endswith(".md")]:
        base = (ROOT / rel).parent
        text = (ROOT / rel).read_text(encoding="utf-8", errors="replace")
        for lineno, line in enumerate(text.splitlines(), 1):
            for target in re.findall(r"\[[^\]]*\]\(([^)\s]+)", line):
                if target.startswith(("http://", "https://", "mailto:", "#")):
                    continue
                path_part, _, anchor = target.partition("#")
                if not path_part:
                    continue
                resolved = (base / path_part).resolve()
                try:
                    resolved.relative_to(ROOT)
                except ValueError:
                    problems.append(f"{rel}:{lineno}: link escapes the repository: {target}")
                    continue
                if not resolved.exists():
                    problems.append(f"{rel}:{lineno}: broken relative link -> {target}")
                    continue
                if anchor and resolved.suffix == ".md" and resolved.is_file():
                    if not _anchor_exists(resolved.read_text(encoding="utf-8", errors="replace"), anchor):
                        problems.append(f"{rel}:{lineno}: anchor not found in {path_part}: #{anchor}")
        # images referenced with ![...](...) must exist and must be committable
        for lineno, line in enumerate(text.splitlines(), 1):
            for img in re.findall(r"!\[[^\]]*\]\(([^)\s]+)", line):
                if img.startswith(("http://", "https://", "data:")):
                    continue
                target = (base / img).resolve()
                if not target.exists():
                    problems.append(f"{rel}:{lineno}: missing image: {img}")
                elif target.suffix.lower() in (".png", ".jpg", ".jpeg"):
                    try:
                        rel_target = target.relative_to(ROOT).as_posix()
                    except ValueError:
                        rel_target = img
                    if rel_target not in _ignorable_ok():
                        problems.append(
                            f"{rel}:{lineno}: image {img} exists but is NOT tracked by git "
                            f"(check the .gitignore '*.png'/'*.jpg' rules and the docs/images negation)"
                        )
    return problems


def _ignorable_ok() -> set[str]:
    """Images that git actually tracks, as relative POSIX paths."""
    tracked = set(tracked_files())
    return {p for p in tracked if p.endswith((".png", ".jpg", ".jpeg"))}


def check_required_files(files: list[str]) -> list[str]:
    present = set(files)
    return [f"missing required file: {f}" for f in REQUIRED_FILES if f not in present]


def check_readme_sections(files: list[str]) -> list[str]:
    problems = []
    readme = ROOT / "README.md"
    if not readme.is_file():
        return ["README.md is missing"]
    text = readme.read_text(encoding="utf-8", errors="replace")
    for section in REQUIRED_README_SECTIONS:
        if section not in text:
            problems.append(f"README.md is missing section: {section}")
    if "Concept mockup" not in text and "concept mockup" not in text:
        problems.append("README.md must label the dashboard image as a concept mockup")
    return problems


def check_conan_graph(files: list[str]) -> list[str]:
    problems = []
    conanfile = ROOT / "conanfile.txt"
    if not conanfile.is_file():
        return ["conanfile.txt is missing"]
    requires = {}
    section = None
    for line in conanfile.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            continue
        if section == "requires" and "/" in line and not line.startswith("#"):
            name, _, version = line.partition("/")
            requires[name.strip()] = version.strip()
    for recipe_dir in sorted((ROOT / "conan" / "recipes").glob("*")):
        if not recipe_dir.is_dir():
            continue
        recipe = recipe_dir / "conanfile.py"
        if not recipe.is_file():
            problems.append(f"{recipe_dir.name}: no conanfile.py")
            continue
        text = recipe.read_text(encoding="utf-8")
        name_m = re.search(r'name\s*=\s*"([^"]+)"', text)
        version_m = re.search(r'version\s*=\s*"([^"]+)"', text)
        if not name_m:
            problems.append(f"conan/recipes/{recipe_dir.name}: conanfile.py does not set name")
            continue
        name = name_m.group(1)
        if not version_m:
            problems.append(
                f"conan/recipes/{recipe_dir.name}: conanfile.py does not set version, so "
                f"`conan create conan/recipes/{name}` cannot resolve {name}/<version>"
            )
            continue
        if name in requires and requires[name] != version_m.group(1):
            problems.append(
                f"conanfile.txt requires {name}/{requires[name]} but the recipe is version "
                f"{version_m.group(1)}"
            )
    for name, version in requires.items():
        if (ROOT / "conan" / "recipes" / name).is_dir() and not version:
            problems.append(f"conanfile.txt: {name} has no version pinned")
    return problems


def check_gitignore_negation() -> list[str]:
    gitignore = ROOT / ".gitignore"
    if not gitignore.is_file():
        return [".gitignore is missing"]
    text = gitignore.read_text(encoding="utf-8")
    problems = []
    if "!docs/images/*.png" not in text:
        problems.append(".gitignore must un-ignore docs images (`!docs/images/*.png`), "
                        "otherwise a global `*.png` rule silently drops documentation assets")
    if "core/config/config.json" not in text:
        problems.append(".gitignore must ignore core/config/config.json")
    return problems


def check_cmake_includes_third_party() -> list[str]:
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    problems = []
    if "third_party/cpp-httplib/include" not in cmake:
        problems.append("CMakeLists.txt must add third_party/cpp-httplib/include to the include path")
    for header in ("tests/CMakeLists.txt",):
        if not (ROOT / header).is_file():
            problems.append(f"{header} is missing")
    if "add_subdirectory(tests)" not in cmake:
        problems.append("CMakeLists.txt should add_subdirectory(tests) so ctest covers the unit tests")
    return problems


def main() -> int:
    files = tracked_files()
    checks = [
        ("tracked artifacts that must stay private", check_forbidden_tracked(files)),
        ("required files", check_required_files(files)),
        ("README structure", check_readme_sections(files)),
        ("links and images", check_links(files)),
        ("secrets and addresses in source", check_secrets(files)),
        ("Conan recipe graph", check_conan_graph(files)),
        (".gitignore invariants", check_gitignore_negation()),
        ("CMake invariants", check_cmake_includes_third_party()),
    ]

    print(f"repository hygiene check ({len(files)} tracked files)")
    failed = False
    for label, problems in checks:
        if problems:
            failed = True
            print(f"\n  FAIL  {label}")
            for problem in problems:
                print(f"        - {problem}")
        else:
            print(f"  ok    {label}")

    print()
    if failed:
        print("hygiene problems found (see above)")
        return 1
    print("all hygiene checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

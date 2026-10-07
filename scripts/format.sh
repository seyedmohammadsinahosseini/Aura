#!/usr/bin/env bash
# Format first-party C++ sources with the repository style, or verify them.
#
#   ./scripts/format.sh            # rewrite files in place
#   ./scripts/format.sh --check    # exit non-zero and list offenders (used by CI)
#
# third_party/ is skipped by its own .clang-format (DisableFormat), and the Conan
# recipes keep upstream formatting, so both are excluded explicitly here too.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

# The version CI installs, kept here so a local run cannot silently disagree with the bot.
# clang-format's output is not stable across releases, so a different version can reflow the
# whole tree; .github/workflows/ci.yml installs exactly this one.
PINNED_CLANG_FORMAT=23.1.3

MODE="format"
if [[ "${1:-}" == "--check" ]]; then
  MODE="check"
elif [[ -n "${1:-}" ]]; then
  echo "usage: $(basename "$0") [--check]" >&2
  exit 2
fi

if ! command -v clang-format >/dev/null 2>&1; then
  echo "error: clang-format not found (pip install clang-format==$PINNED_CLANG_FORMAT)" >&2
  exit 127
fi

CF_VERSION="$(clang-format --version | awk '{print $NF}')"
if [[ "$CF_VERSION" != "$PINNED_CLANG_FORMAT" ]]; then
  echo "note: clang-format $CF_VERSION in PATH, this repository pins $PINNED_CLANG_FORMAT." >&2
  echo "      If --check fails only here, re-run with: pip install clang-format==$PINNED_CLANG_FORMAT" >&2
fi

mapfile -t FILES < <(
  {
    find core modules services ffi http tests -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \)
    printf '%s\n' main.cpp main_admin_server.cpp
  } | sort -u
)

if [[ ${#FILES[@]} -eq 0 ]]; then
  echo "no sources found"
  exit 0
fi

status=0
if [[ "$MODE" == "check" ]]; then
  # --Werror turns style findings into a non-zero exit code; --dry-run keeps files untouched.
  clang-format --style=file --dry-run -Werror "${FILES[@]}" || status=1
  if [[ $status -ne 0 ]]; then
    echo
    echo "Formatting differences found. Run ./scripts/format.sh and commit the result."
    echo "clang-format version: $(clang-format --version)"
  fi
else
  clang-format --style=file -i "${FILES[@]}"
  echo "formatted ${#FILES[@]} files with $(clang-format --version)"
fi

exit $status

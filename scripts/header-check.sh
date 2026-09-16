#!/usr/bin/env bash
#
# Public header self-containment.
#
# Every public header of `pigeon::core` must compile on its own. A header that
# only compiles because some *other* header was included first is a latent
# build failure: it is invisible for as long as every translation unit happens
# to include its accidental provider first, and it surfaces on the day somebody
# writes the one file that does not - usually the first implementation file for
# that very header.
#
# core/src/public_headers.cpp deliberately includes every public header so that
# none goes uncompiled, but it cannot catch this. Including them together, in
# one translation unit, in alphabetical order, proves only that the *set*
# compiles. Self-containment is a property of each header *alone*, and the only
# way to observe it is to compile each one alone. That is what this does.
#
# Verifies the source-tree half of:
#   REQ-DEV-001  the system builds from a clean checkout with no hardware
#
# Scope is core/ only. raspberry/ and arduino/ are hardware layers whose
# headers need a cross-toolchain this gate does not have and must not assume.
#
# Usage:
#   scripts/header-check.sh              # check this repository
#   scripts/header-check.sh --root DIR   # check an arbitrary tree (used by tests)
#
# Environment:
#   CXX                    Compiler to use. Defaults to the first of c++,
#                          clang++, g++ found on PATH.
#   PIGEON_STRICT_TOOLS=1  Fail (instead of warn) when no compiler is found.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STRICT_TOOLS="${PIGEON_STRICT_TOOLS:-0}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --root) ROOT="$2"; shift 2 ;;
    -h|--help) sed -n '2,31p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [[ ! -d "$ROOT" ]]; then
  echo "error: root directory not found: $ROOT" >&2
  exit 2
fi

violations=0

printf '\n\033[1m==> Checking public header self-containment\033[0m\n'

INCLUDE_DIR="$ROOT/core/include"

headers="$(
  [[ -d "$INCLUDE_DIR" ]] &&
    find "$INCLUDE_DIR" -type f -name '*.hpp' 2>/dev/null | LC_ALL=C sort || true
)"

if [[ -z "$headers" ]]; then
  printf '\033[32m    ok: no public headers to check\033[0m\n'
  exit 0
fi

# The language standard is not restated here. Two places to change one fact is
# how a gate starts checking something the build no longer does, and this gate
# is worthless if it disagrees with the compiler that matters.
standard=''
if [[ -f "$ROOT/CMakeLists.txt" ]]; then
  standard="$(sed -n 's/.*set(CMAKE_CXX_STANDARD[[:space:]]\{1,\}\([0-9]\{2\}\).*/\1/p' \
    "$ROOT/CMakeLists.txt" | head -1)"
fi
if [[ -z "$standard" ]]; then
  standard=20
fi

compiler="${CXX:-}"
if [[ -z "$compiler" ]]; then
  for candidate in c++ clang++ g++; do
    if command -v "$candidate" > /dev/null 2>&1; then
      compiler="$candidate"
      break
    fi
  done
fi

if [[ -z "$compiler" ]]; then
  if [[ "$STRICT_TOOLS" == "1" ]]; then
    printf '\033[31m    FAILED: no C++ compiler found.\033[0m\n' >&2
    exit 1
  fi
  printf '\033[33m    warning: no C++ compiler found - step skipped\033[0m\n'
  exit 0
fi

probe_dir="$(mktemp -d "${TMPDIR:-/tmp}/pigeon-header-check.XXXXXX")"
trap 'rm -rf "$probe_dir"' EXIT

probe="$probe_dir/probe.cpp"
log="$probe_dir/probe.log"

while IFS= read -r header; do
  [[ -z "$header" ]] && continue
  relative="${header#"$INCLUDE_DIR"/}"

  # The header is included by its public path and nothing else is in scope, so
  # anything it needs and does not include is a diagnostic rather than a lucky
  # inheritance from a neighbour.
  printf '#include "%s"\n' "$relative" > "$probe"

  if ! "$compiler" -std="c++${standard}" -I "$INCLUDE_DIR" \
    -fsyntax-only "$probe" > "$log" 2>&1; then
    violations=$((violations + 1))
    printf '    violation: %s is not self-contained\n' "core/include/$relative"
    sed 's/^/        | /' "$log" | head -5
  fi
done <<< "$headers"

if [[ "$violations" -gt 0 ]]; then
  printf '\033[31m    FAILED: %d header(s) do not compile on their own.\033[0m\n' \
    "$violations" >&2
  printf '    A header must include what it uses. Compiling cleanly inside\n' >&2
  printf '    core/src/public_headers.cpp proves only that some earlier\n' >&2
  printf '    #include happened to supply the missing declaration.\n' >&2
  exit 1
fi

printf '\033[32m    ok: %s public header(s) compile on their own\033[0m\n' \
  "$(printf '%s\n' "$headers" | wc -l | tr -d ' ')"

#!/usr/bin/env bash
#
# The single source of truth for "is this repository healthy?".
#
# Every agent and every human must be able to run this one command and get a
# pass/fail answer. CI runs exactly this script, so a green run locally means
# a green run in CI.
#
# Usage:
#   scripts/check.sh                 # configure + build + test + format + tidy
#   scripts/check.sh --preset macos-release
#   scripts/check.sh --skip-tidy     # faster inner loop
#   scripts/check.sh --fix           # rewrite files with clang-format
#
# Environment:
#   PIGEON_STRICT_TOOLS=1  Fail (instead of warn) when clang-format or
#                          clang-tidy are not installed. Set this in CI.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

PRESET="macos-debug"
SKIP_TIDY=0
SKIP_FORMAT=0
SKIP_TESTS=0
FIX=0
STRICT_TOOLS="${PIGEON_STRICT_TOOLS:-0}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)      PRESET="$2"; shift 2 ;;
    --skip-tidy)   SKIP_TIDY=1; shift ;;
    --skip-format) SKIP_FORMAT=1; shift ;;
    --skip-tests)  SKIP_TESTS=1; shift ;;
    --fix)         FIX=1; shift ;;
    -h|--help)     sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *)             echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

BUILD_DIR="build/${PRESET}"

step()  { printf '\n\033[1m==> %s\033[0m\n' "$1"; }
ok()    { printf '\033[32m    ok: %s\033[0m\n' "$1"; }
warn()  { printf '\033[33m    warning: %s\033[0m\n' "$1"; }
fail()  { printf '\033[31m    FAILED: %s\033[0m\n' "$1"; exit 1; }

# clang-format / clang-tidy are not part of the Xcode command line tools, so
# look in the usual Homebrew LLVM locations before giving up.
find_tool() {
  local name="$1"
  local candidate
  for candidate in \
    "$(command -v "$name" 2>/dev/null || true)" \
    "/opt/homebrew/opt/llvm/bin/$name" \
    "/usr/local/opt/llvm/bin/$name"; do
    if [[ -n "$candidate" && -x "$candidate" ]]; then
      echo "$candidate"
      return 0
    fi
  done
  return 1
}

missing_tool() {
  local name="$1"
  if [[ "$STRICT_TOOLS" == "1" ]]; then
    fail "$name not found. Install it with: brew install llvm"
  fi
  warn "$name not found - step skipped. Install it with: brew install llvm"
}

# Source files owned by this project (excludes build output and dependencies).
# --others --exclude-standard also covers files that are not yet committed, so
# a new file cannot slip through the gate unformatted.
project_sources() {
  git ls-files --cached --others --exclude-standard \
    -- '*.cpp' '*.hpp' '*.h' '*.cc' '*.ino'
}

# macOS ships bash 3.2, which has no `mapfile`, so read into an array manually.
read_into_array() {
  local line
  read_result=()
  while IFS= read -r line; do
    [[ -n "$line" ]] && read_result+=("$line")
  done
}

# ---------------------------------------------------------------- configure --
step "Configuring ($PRESET)"
cmake --preset "$PRESET" > /dev/null
ok "configured in $BUILD_DIR"

# -------------------------------------------------------------------- build --
step "Building ($PRESET)"
cmake --build --preset "$PRESET"
ok "build succeeded"

# -------------------------------------------------------------------- tests --
if [[ "$SKIP_TESTS" == "0" ]]; then
  step "Running tests ($PRESET)"
  ctest --preset "$PRESET"
  ok "all tests passed"
else
  warn "tests skipped"
fi

# ------------------------------------------------------------------- format --
if [[ "$SKIP_FORMAT" == "0" ]]; then
  step "Checking formatting"
  if CLANG_FORMAT="$(find_tool clang-format)"; then
    read_into_array < <(project_sources)
    files=("${read_result[@]:-}")
    if [[ -z "${files[0]:-}" ]]; then
      ok "no source files to format"
    elif [[ "$FIX" == "1" ]]; then
      "$CLANG_FORMAT" -i "${files[@]}"
      ok "reformatted ${#files[@]} file(s)"
    else
      "$CLANG_FORMAT" --dry-run --Werror "${files[@]}" \
        || fail "formatting violations. Run: scripts/check.sh --fix"
      ok "${#files[@]} file(s) correctly formatted"
    fi
  else
    missing_tool clang-format
  fi
else
  warn "formatting check skipped"
fi

# --------------------------------------------------------------------- tidy --
if [[ "$SKIP_TIDY" == "0" ]]; then
  step "Running clang-tidy"
  if CLANG_TIDY="$(find_tool clang-tidy)"; then
    if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
      fail "compile_commands.json missing in $BUILD_DIR"
    fi
    read_into_array < <(project_sources | grep -E '\.(cpp|cc)$' | grep -v '^tests/' || true)
    tidy_files=("${read_result[@]:-}")

    # Homebrew/pip builds of clang-tidy do not know where the macOS SDK is, so
    # the standard library headers are invisible unless we point at it.
    tidy_args=()
    if [[ "$(uname -s)" == "Darwin" ]] && SDK_PATH="$(xcrun --show-sdk-path 2>/dev/null)"; then
      tidy_args+=("--extra-arg=-isysroot" "--extra-arg=$SDK_PATH")
    fi

    if [[ -z "${tidy_files[0]:-}" ]]; then
      ok "no source files to analyse"
    else
      "$CLANG_TIDY" -p "$BUILD_DIR" --quiet "${tidy_args[@]:-}" "${tidy_files[@]}" \
        || fail "clang-tidy reported issues"
      ok "${#tidy_files[@]} file(s) analysed"
    fi
  else
    missing_tool clang-tidy
  fi
else
  warn "clang-tidy skipped"
fi

printf '\n\033[1;32mAll checks passed.\033[0m\n'

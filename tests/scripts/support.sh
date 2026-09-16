#!/usr/bin/env bash
#
# Minimal assertion helpers for the gate-script test suite.
#
# These tests verify the enforcement scripts themselves. An enforcement script
# that has never been observed to reject a violation is an unproven claim, so
# every check here plants a real violation into a throwaway tree and asserts
# that the script fails on it.

set -uo pipefail

: "${PIGEON_REPO_ROOT:?PIGEON_REPO_ROOT must be set by the test harness}"

_pass=0
_fail=0
_sandboxes=()

_ok() {
  printf '    ok: %s\n' "$1"
  _pass=$((_pass + 1))
}

_bad() {
  printf '    FAILED: %s\n' "$1"
  _fail=$((_fail + 1))
}

# expect_exit <description> <expected-status> <command...>
#
# Runs the command, captures its combined output, and compares the exit status.
# On mismatch the captured output is printed so the failure is diagnosable.
expect_exit() {
  local description="$1"
  local expected="$2"
  shift 2

  local output
  local status
  if output="$("$@" 2>&1)"; then
    status=0
  else
    status=$?
  fi

  if [[ "$status" == "$expected" ]]; then
    _ok "$description"
  else
    _bad "$description (expected exit $expected, got $status)"
    printf '%s\n' "$output" | sed 's/^/        | /'
  fi
}

# expect_output_contains <description> <needle> <command...>
expect_output_contains() {
  local description="$1"
  local needle="$2"
  shift 2

  local output
  output="$("$@" 2>&1 || true)"

  if printf '%s' "$output" | grep -qF -- "$needle"; then
    _ok "$description"
  else
    _bad "$description (output did not mention '$needle')"
    printf '%s\n' "$output" | sed 's/^/        | /'
  fi
}

# expect_output_not_contains — asserts a string is absent from the output.
#
# The mirror of expect_output_contains. Needed where the interesting claim is
# that the gate stayed silent about something, e.g. that it did not invent a
# requirement from an illustration.
expect_output_not_contains() {
  local description="$1"
  local needle="$2"
  shift 2

  local output
  output="$("$@" 2>&1 || true)"

  if printf '%s' "$output" | grep -qF -- "$needle"; then
    _bad "$description (output unexpectedly mentioned '$needle')"
    printf '%s\n' "$output" | sed 's/^/        | /'
  else
    _ok "$description"
  fi
}

# new_sandbox — prints the path to a fresh, minimal, well-formed source tree.
#
# The tree passes every gate by default, so each test only has to introduce the
# single violation it is about.
new_sandbox() {
  local dir
  dir="$(mktemp -d "${TMPDIR:-/tmp}/pigeon-gate-test.XXXXXX")"
  _sandboxes+=("$dir")

  mkdir -p "$dir/core/include/pigeon/core" "$dir/core/src" \
           "$dir/tests/core" "$dir/docs/requirements"

  cat > "$dir/core/include/pigeon/core/example.hpp" <<'EOF'
#pragma once

#include <string_view>

namespace pigeon::core {
[[nodiscard]] std::string_view example() noexcept;
}  // namespace pigeon::core
EOF

  cat > "$dir/core/src/example.cpp" <<'EOF'
#include "pigeon/core/example.hpp"

namespace pigeon::core {
std::string_view example() noexcept { return "example"; }
}  // namespace pigeon::core
EOF

  cat > "$dir/tests/core/example_test.cpp" <<'EOF'
// Verifies: REQ-EXA-001
#include <gtest/gtest.h>
namespace {
TEST(Example, Works) { EXPECT_TRUE(true); }
}  // namespace
EOF

  cat > "$dir/docs/requirements/requirements.md" <<'EOF'
# Requirements fixture

### REQ-EXA-001 — Example requirement

**Statement:** Placeholder.
EOF

  printf '%s' "$dir"
}

# write_file <path> — creates parent directories, then writes stdin to the file.
write_file() {
  mkdir -p "$(dirname "$1")"
  cat > "$1"
}

cleanup_sandboxes() {
  local dir
  for dir in ${_sandboxes+"${_sandboxes[@]}"}; do
    [[ -n "$dir" && -d "$dir" ]] && rm -rf "$dir"
  done
}

finish() {
  cleanup_sandboxes
  printf '  %d passed, %d failed\n' "$_pass" "$_fail"
  if [[ "$_fail" -gt 0 ]]; then
    exit 1
  fi
  exit 0
}

trap cleanup_sandboxes EXIT

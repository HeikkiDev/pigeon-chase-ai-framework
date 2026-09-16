#!/usr/bin/env bash
#
# Verifies: REQ-DET-002 (core/ contains no camera, GPIO, Raspberry Pi or
#           Arduino header), REQ-DEV-001 (hardware-free build and test),
#           REQ-DEV-003 (the default suite opens no serial port or camera).
#
# These acceptance criteria are properties of the source tree, not of a running
# program, so they are verified by running scripts/arch-check.sh against trees
# that deliberately violate them.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIGEON_REPO_ROOT="${PIGEON_REPO_ROOT:-$(cd "$HERE/../.." && pwd)}"
export PIGEON_REPO_ROOT
# shellcheck source=tests/scripts/support.sh
source "$HERE/support.sh"

ARCH_CHECK="$PIGEON_REPO_ROOT/scripts/arch-check.sh"

echo "arch-check.sh"

# -------------------------------------------------------------- nominal case --
sandbox="$(new_sandbox)"
expect_exit "accepts a clean tree" 0 "$ARCH_CHECK" --root "$sandbox"

# ------------------------------------------- hardware headers in core/ (DET-002) --
for header in 'wiringPi.h' 'pigpio.h' 'Arduino.h' 'termios.h' 'opencv2/opencv.hpp' 'linux/videodev2.h'; do
  sandbox="$(new_sandbox)"
  write_file "$sandbox/core/src/offender.cpp" <<EOF
#include <${header}>

namespace pigeon::core {
void offend() {}
}  // namespace pigeon::core
EOF
  expect_exit "rejects <${header}> in core/" 1 "$ARCH_CHECK" --root "$sandbox"
done

sandbox="$(new_sandbox)"
write_file "$sandbox/core/include/pigeon/core/offender.hpp" <<'EOF'
#pragma once
#include "Arduino.h"
EOF
expect_exit 'rejects a quoted hardware include in a core/ header' 1 "$ARCH_CHECK" --root "$sandbox"

sandbox="$(new_sandbox)"
write_file "$sandbox/core/src/offender.cpp" <<'EOF'
#include <wiringPi.h>
EOF
expect_output_contains 'names the offending file and header' 'wiringPi.h' \
  "$ARCH_CHECK" --root "$sandbox"

# ------------------------------------------------------- I/O in core/ (DEV-001) --
for header in 'iostream' 'fstream' 'cstdio' 'filesystem'; do
  sandbox="$(new_sandbox)"
  write_file "$sandbox/core/src/offender.cpp" <<EOF
#include <${header}>
EOF
  expect_exit "rejects <${header}> in core/ (domain logic performs no I/O)" 1 \
    "$ARCH_CHECK" --root "$sandbox"
done

# ------------------------------------------- wall clock and rand in core/ (DEV-002) --
sandbox="$(new_sandbox)"
write_file "$sandbox/core/src/offender.cpp" <<'EOF'
#include <chrono>

namespace pigeon::core {
auto stamp() { return std::chrono::steady_clock::now(); }
}  // namespace pigeon::core
EOF
expect_exit 'rejects a wall-clock read in core/' 1 "$ARCH_CHECK" --root "$sandbox"

sandbox="$(new_sandbox)"
write_file "$sandbox/core/src/offender.cpp" <<'EOF'
#include <cstdlib>

namespace pigeon::core {
int pick() { return std::rand(); }
}  // namespace pigeon::core
EOF
expect_exit 'rejects std::rand() in core/' 1 "$ARCH_CHECK" --root "$sandbox"

# A seeded engine is deterministic and is explicitly endorsed by the testing
# conventions, so <chrono> durations and <random> engines must remain legal.
sandbox="$(new_sandbox)"
write_file "$sandbox/core/src/duration.cpp" <<'EOF'
#include <chrono>
#include <random>

namespace pigeon::core {
constexpr std::chrono::milliseconds kMaxFire{750};
std::mt19937 seeded(unsigned int seed) { return std::mt19937{seed}; }
}  // namespace pigeon::core
EOF
expect_exit 'accepts chrono durations and seeded engines in core/' 0 \
  "$ARCH_CHECK" --root "$sandbox"

# ------------------------------------------- hardware in the default suite (DEV-003) --
sandbox="$(new_sandbox)"
write_file "$sandbox/tests/core/serial_test.cpp" <<'EOF'
// Verifies: REQ-EXA-001
#include <termios.h>
EOF
expect_exit 'rejects a hardware header in the default test suite' 1 \
  "$ARCH_CHECK" --root "$sandbox"

sandbox="$(new_sandbox)"
write_file "$sandbox/tests/core/device_test.cpp" <<'EOF'
// Verifies: REQ-EXA-001
const char* port = "/dev/ttyUSB0";
EOF
expect_exit 'rejects a serial device path in the default test suite' 1 \
  "$ARCH_CHECK" --root "$sandbox"

sandbox="$(new_sandbox)"
write_file "$sandbox/tests/core/camera_test.cpp" <<'EOF'
// Verifies: REQ-EXA-001
const char* cam = "/dev/video0";
EOF
expect_exit 'rejects a camera device path in the default test suite' 1 \
  "$ARCH_CHECK" --root "$sandbox"

# --------------------------------------------------- hardware layers stay legal --
sandbox="$(new_sandbox)"
write_file "$sandbox/raspberry/camera.cpp" <<'EOF'
#include <termios.h>
#include <iostream>
EOF
write_file "$sandbox/arduino/firmware.ino" <<'EOF'
#include <Arduino.h>
EOF
expect_exit 'permits hardware headers in raspberry/ and arduino/' 0 \
  "$ARCH_CHECK" --root "$sandbox"

# --------------------------------------- exhaustive refusal mapping (ADR-0015) --
# REQ-SAF-008 requires every LinkStatus to carry its own refusal reason. The
# guarantee that no status is forgotten rests on -Wswitch, which only fires when
# the switch has no `default:` label. A `default:` therefore silently disarms the
# compiler check and is invisible in review, so the gate has to see it.
sandbox="$(new_sandbox)"
write_file "$sandbox/core/src/safety_policy.cpp" <<'EOF'
namespace pigeon::core {
RefusalReason refusal_for(LinkStatus status) {
  switch (status) {
    case LinkStatus::OK: return RefusalReason::NONE;
    case LinkStatus::UNAVAILABLE: return RefusalReason::LINK_UNAVAILABLE;
    default: return RefusalReason::LINK_UNAVAILABLE;
  }
}
}  // namespace pigeon::core
EOF
expect_exit 'rejects a default: label in refusal_for (ADR-0015)' 1 \
  "$ARCH_CHECK" --root "$sandbox"

expect_output_contains 'names ADR-0015 when rejecting the default: label' \
  'ADR-0015' "$ARCH_CHECK" --root "$sandbox"

# The ban is on defeating -Wswitch inside that one function, not on `default:`
# as a keyword. A switch elsewhere in the same file is ordinary C++.
sandbox="$(new_sandbox)"
write_file "$sandbox/core/src/safety_policy.cpp" <<'EOF'
namespace pigeon::core {
RefusalReason refusal_for(LinkStatus status) {
  switch (status) {
    case LinkStatus::OK: return RefusalReason::NONE;
    case LinkStatus::UNAVAILABLE: return RefusalReason::LINK_UNAVAILABLE;
    case LinkStatus::TRANSPORT_FAILURE: return RefusalReason::LINK_TRANSPORT_FAILURE;
    case LinkStatus::REJECTED: return RefusalReason::LINK_REJECTED;
  }
  return RefusalReason::LINK_UNAVAILABLE;
}

const char* describe(Phase phase) {
  switch (phase) {
    case Phase::SEARCHING: return "searching";
    default: return "other";
  }
}
}  // namespace pigeon::core
EOF
expect_exit 'permits default: in an unrelated switch in the same file' 0 \
  "$ARCH_CHECK" --root "$sandbox"

# Naming the function is not defining it. A gate that arms itself on any mention
# would report a `default:` belonging to some entirely unrelated function later
# in the file, and a gate that cries wolf is one people start passing with -k.
sandbox="$(new_sandbox)"
write_file "$sandbox/core/src/safety_policy.hpp" <<'EOF'
namespace pigeon::core {
// See refusal_for(LinkStatus) in safety_policy.cpp for the mapping
// used when the link is not healthy

/* The block comment below also names refusal_for(LinkStatus) without
   defining it anywhere. */

const char* describe(Phase phase) {
  switch (phase) {
    case Phase::SEARCHING: return "searching";
    default: return "other";
  }
}
}  // namespace pigeon::core
EOF
expect_exit 'permits default: when refusal_for is only named in a comment' 0 \
  "$ARCH_CHECK" --root "$sandbox"

sandbox="$(new_sandbox)"
write_file "$sandbox/core/src/safety_policy.hpp" <<'EOF'
namespace pigeon::core {
static_assert(true, "give the new status its own case in refusal_for()");

const char* describe(Phase phase) {
  switch (phase) {
    case Phase::SEARCHING: return "searching";
    default: return "other";
  }
}
}  // namespace pigeon::core
EOF
expect_exit 'permits default: when refusal_for is only named in a string' 0 \
  "$ARCH_CHECK" --root "$sandbox"

# The function does not exist yet. A gate that demands its presence would fail
# the whole repository until it is written, so absence is not a violation.
sandbox="$(new_sandbox)"
expect_exit 'permits a tree in which refusal_for does not yet exist' 0 \
  "$ARCH_CHECK" --root "$sandbox"

# ------------------------------------------------------------ the real repository --
expect_exit 'the real repository satisfies its own architectural rules' 0 \
  "$ARCH_CHECK" --root "$PIGEON_REPO_ROOT"

finish

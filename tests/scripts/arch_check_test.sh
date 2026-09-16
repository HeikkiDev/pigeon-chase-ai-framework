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

# ------------------------------------------------------------ the real repository --
expect_exit 'the real repository satisfies its own architectural rules' 0 \
  "$ARCH_CHECK" --root "$PIGEON_REPO_ROOT"

finish

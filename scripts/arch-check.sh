#!/usr/bin/env bash
#
# Architectural rule enforcement.
#
# AGENTS.md, docs/architecture/architecture.md and
# .github/instructions/core.instructions.md all assert that `core/` is free of
# hardware, platform and I/O dependencies, and that the default test suite
# touches no physical device. Until this script existed those were prose: a
# rule that nothing checks is a rule that will eventually be broken.
#
# Verifies the source-tree half of:
#   REQ-DET-002  core/ contains no camera, GPIO, Raspberry Pi or Arduino header
#   REQ-DEV-001  the system builds, runs and is tested with no hardware present
#   REQ-DEV-002  no wall clock or unseeded randomness in domain logic
#   REQ-DEV-003  the default suite opens no serial port or camera device
#
# Usage:
#   scripts/arch-check.sh                # check this repository
#   scripts/arch-check.sh --root DIR     # check an arbitrary tree (used by tests)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --root) ROOT="$2"; shift 2 ;;
    -h|--help) sed -n '2,22p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [[ ! -d "$ROOT" ]]; then
  echo "error: root directory not found: $ROOT" >&2
  exit 2
fi

violations=0

report() {
  violations=$((violations + 1))
  printf '    violation: %s\n' "$1"
}

# Hardware, platform and OS headers. Legal in raspberry/ and arduino/, never in
# core/ and never in the default test suite.
readonly HARDWARE_INCLUDES='wiringPi\.h|pigpio\.h|bcm2835\.h|gpiod\.h|Arduino\.h|Servo\.h|Wire\.h|SPI\.h|EEPROM\.h|termios\.h|sys/ioctl\.h|sys/socket\.h|netinet/|arpa/|linux/|opencv2/|libcamera|raspicam|v4l2|windows\.h'

# I/O headers. Domain logic returns data; the caller decides what to do with it.
readonly IO_INCLUDES='iostream|fstream|cstdio|stdio\.h|filesystem|syslog\.h|unistd\.h|fcntl\.h'

# Non-deterministic calls. Note that <chrono> durations and explicitly seeded
# engines are deliberately permitted: the ban is on reading the wall clock and
# on unseeded randomness, not on the vocabulary types for time and generators.
readonly NONDETERMINISM='(system_clock|steady_clock|high_resolution_clock)[[:space:]]*::[[:space:]]*now|[^_[:alnum:]](std::)?s?rand[[:space:]]*\(|random_device|getenv[[:space:]]*\(|std::(cout|cerr|clog)'

# Physical device nodes, in any layer that is meant to run without hardware.
readonly DEVICE_PATHS='/dev/tty|/dev/video|/dev/i2c|/dev/spidev|/dev/serial'

# Only first-party C++ sources are scanned. Shell, CMake and Markdown files may
# legitimately mention a forbidden header by name — this file does.
sources_in() {
  local dir="$1"
  [[ -d "$dir" ]] || return 0
  find "$dir" \
    -type d -name build -prune -o \
    -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.h' -o -name '*.hpp' \) -print \
    2>/dev/null | LC_ALL=C sort
}

# scan <directory> <description> <pattern> [include-directive-only]
scan() {
  local dir="$1" description="$2" pattern="$3" includes_only="${4:-0}"
  local effective="$pattern"

  if [[ "$includes_only" == "1" ]]; then
    effective="^[[:space:]]*#[[:space:]]*include[[:space:]]*[<\"][^>\"]*(${pattern})"
  fi

  local file line
  while IFS= read -r file; do
    [[ -z "$file" ]] && continue
    while IFS= read -r line; do
      [[ -z "$line" ]] && continue
      report "${description}: ${file#"$ROOT"/}:${line}"
    done < <(grep -nE -- "$effective" "$file" 2>/dev/null | sed 's/[[:space:]]\{1,\}/ /g' || true)
  done < <(sources_in "$dir")
}

# The refusal mapping must keep -Wswitch armed (ADR-0015, REQ-SAF-008).
#
# Every LinkStatus has to carry its own refusal reason. Nothing in the language
# guarantees that except -Wswitch, which warns about an unhandled enumerator
# only while the switch has no `default:` label. Adding one is a single word, is
# easy to justify to yourself as defensive, and silently disarms the only
# mechanism that would notice a status added later. It is invisible in review
# precisely because it looks careful, so the gate has to see it.
#
# Scoped to the body of refusal_for: this bans defeating the exhaustiveness
# check in the one function that depends on it, not the keyword in general.
check_refusal_switch() {
  local file line
  while IFS= read -r file; do
    [[ -z "$file" ]] && continue
    while IFS= read -r line; do
      [[ -z "$line" ]] && continue
      report "default: in refusal_for defeats -Wswitch (ADR-0015, REQ-SAF-008): ${file#"$ROOT"/}:${line}"
    done < <(awk '
      {
        # Match against code only. Naming the function in a comment or a
        # diagnostic string is not defining it, and arming on a mention would
        # blame this function for a `default:` belonging to another one.
        code = $0
        if (in_block) {
          if (sub(/^.*\*\//, "", code)) { in_block = 0 } else { code = "" }
        }
        gsub(/"[^"]*"/, "\"\"", code)
        gsub(/\/\*[^*]*\*\//, " ", code)
        if (sub(/\/\*.*$/, " ", code)) { in_block = 1 }
        sub(/\/\/.*$/, "", code)
      }
      # Entering a candidate definition of refusal_for.
      state == 0 && code ~ /refusal_for[[:space:]]*\(/ { state = 1; depth = 0; opened = 0 }
      state == 1 {
        # A prototype or a call, not a definition: no body to police.
        if (opened == 0 && index(code, ";") > 0 && index(code, "{") == 0) { state = 0; next }
        n = gsub(/\{/, "{", code); depth += n; if (n > 0) opened = 1
        if (opened && code ~ /(^|[^[:alnum:]_])default[[:space:]]*:/) print NR
        depth -= gsub(/\}/, "}", code)
        if (opened && depth <= 0) state = 0
      }
    ' "$file" 2>/dev/null || true)
  done < <(sources_in "$ROOT/core")
}

printf '\n\033[1m==> Checking architectural rules\033[0m\n'

scan "$ROOT/core"  'hardware or platform header in core/ (REQ-DET-002)' "$HARDWARE_INCLUDES" 1
scan "$ROOT/core"  'I/O header in core/ (REQ-DEV-001)'                   "$IO_INCLUDES" 1
scan "$ROOT/core"  'non-deterministic call in core/ (REQ-DEV-002)'       "$NONDETERMINISM"
scan "$ROOT/core"  'device path in core/ (REQ-DEV-001)'                  "$DEVICE_PATHS"
scan "$ROOT/tests" 'hardware header in the default test suite (REQ-DEV-003)' "$HARDWARE_INCLUDES" 1
scan "$ROOT/tests" 'device path in the default test suite (REQ-DEV-003)' "$DEVICE_PATHS"
check_refusal_switch

if [[ "$violations" -gt 0 ]]; then
  printf '\033[31m    FAILED: %d architectural violation(s).\033[0m\n' "$violations" >&2
  printf '    These are architectural defects, not style issues. Move the\n' >&2
  printf '    dependency behind an interface in core/ and implement it in\n' >&2
  printf '    raspberry/ or arduino/.\n' >&2
  exit 1
fi

printf '\033[32m    ok: core/ is hardware-free and the default suite needs no device\033[0m\n'

#!/usr/bin/env bash
#
# Verifies: REQ-DEV-002 (repeated and shuffled runs produce identical results),
#           REQ-DEV-001.
#
# The determinism step of scripts/check.sh used to send ctest's output to
# /dev/null, so when a test failed under shuffling or repetition the gate said
# only "tests are not deterministic" and nobody could tell which test, or whether
# it was a real order dependence or a flake. CI hit exactly that once: a failed
# run on `main` whose log named no test.
#
# These tests run a copy of check.sh against a stub `ctest`, so the step can be
# made to pass and to fail on any host. Every other step is skipped, and `cmake`
# is a no-op stub.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIGEON_REPO_ROOT="${PIGEON_REPO_ROOT:-$(cd "$HERE/../.." && pwd)}"
export PIGEON_REPO_ROOT
# shellcheck source=tests/scripts/support.sh
source "$HERE/support.sh"

echo "check.sh (determinism step)"

# new_check_tree <ctest-behaviour> — a sandbox with a copy of check.sh and stub
# tools. The ctest stub succeeds on an ordinary run. On the shuffled, repeated
# run it records its arguments and either passes quietly or, when told to fail,
# prints what a real failing ctest prints and exits non-zero.
new_check_tree() {
  local behaviour="$1"
  local dir
  dir="$(new_sandbox)"

  mkdir -p "$dir/scripts" "$dir/bin"
  cp "$PIGEON_REPO_ROOT/scripts/check.sh" "$dir/scripts/check.sh"

  write_file "$dir/bin/cmake" <<'EOF2'
#!/usr/bin/env bash
exit 0
EOF2
  write_file "$dir/bin/ctest" <<EOF2
#!/usr/bin/env bash
case " \$* " in
  *" --schedule-random "*)
    printf '[%s]' "\$@" > "\$(dirname "\$0")/../determinism-args"
    echo "stub ctest: ran the shuffled suite"
    if [[ "$behaviour" == "fail" ]]; then
      echo "The following tests FAILED:"
      echo "	 42 - FlakyTest.OrderDependent (Failed)"
      exit 8
    fi
    ;;
esac
exit 0
EOF2
  chmod +x "$dir/bin/"*

  printf '%s' "$dir"
}

run_gate() {
  local dir="$1"
  PATH="$dir/bin:$PATH" "$dir/scripts/check.sh" \
    --skip-format --skip-tidy --skip-arch --skip-trace --skip-workflow
}

# ----------------------------------------------------------------- passing --
sandbox="$(new_check_tree pass)"
expect_exit 'passes when the shuffled, repeated suite passes' 0 run_gate "$sandbox"
expect_exit 'asks ctest for a random order, two passes and output on failure' 0 \
  grep -qxF -- "[--preset][macos-debug][--schedule-random][--repeat][until-fail:2][--output-on-failure]" \
  "$sandbox/determinism-args"

sandbox="$(new_check_tree pass)"
expect_output_not_contains 'stays quiet about the shuffled suite while it passes' \
  'stub ctest: ran the shuffled suite' run_gate "$sandbox"

# ----------------------------------------------------------------- failing --
sandbox="$(new_check_tree fail)"
expect_exit 'fails when the shuffled, repeated suite fails' 1 run_gate "$sandbox"

sandbox="$(new_check_tree fail)"
expect_output_contains 'names the test that failed' \
  'FlakyTest.OrderDependent' run_gate "$sandbox"

sandbox="$(new_check_tree fail)"
expect_output_contains 'still says which requirement it is about' \
  'REQ-DEV-002' run_gate "$sandbox"

finish

#!/usr/bin/env bash
#
# The single source of truth for "is this repository healthy?".
#
# Every agent and every human must be able to run this one command and get a
# pass/fail answer. CI runs exactly this script, so a green run locally means
# a green run in CI.
#
# Usage:
#   scripts/check.sh                 # the full gate
#   scripts/check.sh --preset macos-release
#   scripts/check.sh --skip-tidy     # faster inner loop
#   scripts/check.sh --fix           # rewrite files with clang-format
#
# Steps, in order:
#   formatting, configure, build, test, architectural rules, determinism,
#   traceability, workflow evidence, clang-tidy.
#
# Formatting runs first because it needs nothing the build produces. Running
# it last meant a failing build hid it entirely (ADR-0017).
#
# Each step is skippable for the inner loop (--skip-tests, --skip-arch,
# --skip-determinism, --skip-trace, --skip-workflow, --skip-format,
# --skip-tidy) but CI runs all of them.
#
# Environment:
#   PIGEON_STRICT_TOOLS=1  Fail (instead of warn) when clang-format or
#                          clang-tidy are not installed. Set this in CI.
#   PIGEON_COMMIT_RANGE    Commit range for the workflow-evidence check.
#                          Defaults to origin/main..HEAD.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

PRESET="macos-debug"
SKIP_TIDY=0
SKIP_FORMAT=0
SKIP_TESTS=0
SKIP_ARCH=0
SKIP_DETERMINISM=0
SKIP_TRACE=0
SKIP_WORKFLOW=0
FIX=0
STRICT_TOOLS="${PIGEON_STRICT_TOOLS:-0}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)           PRESET="$2"; shift 2 ;;
    --skip-tidy)        SKIP_TIDY=1; shift ;;
    --skip-format)      SKIP_FORMAT=1; shift ;;
    --skip-tests)       SKIP_TESTS=1; shift ;;
    --skip-arch)        SKIP_ARCH=1; shift ;;
    --skip-determinism) SKIP_DETERMINISM=1; shift ;;
    --skip-trace)       SKIP_TRACE=1; shift ;;
    --skip-workflow)    SKIP_WORKFLOW=1; shift ;;
    --fix)              FIX=1; shift ;;
    -h|--help)     sed -n '2,30p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
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

# The tool versions this repository is checked with (ADR-0022). Different major
# versions of clang-format and clang-tidy disagree about valid code, so a gate that
# passed locally failed in CI until the versions were fixed in one place.
PIN_FILE="$REPO_ROOT/scripts/lint-tools.txt"

# check_tool_version <name> <path> — compares the tool's version with the pin. A
# difference is a warning, and a failure under PIGEON_STRICT_TOOLS=1, which CI
# sets, so CI can never silently run a tool it was not meant to.
check_tool_version() {
  local name="$1"
  local path="$2"
  local want have message

  if [[ ! -f "$PIN_FILE" ]]; then
    warn "scripts/lint-tools.txt is missing - the $name version is not checked"
    return 0
  fi
  want="$(sed -n "s/^${name}==\(.*\)\$/\1/p" "$PIN_FILE")"
  [[ -n "$want" ]] || return 0

  # Whatever precedes "version" differs (Homebrew, Ubuntu, "LLVM"); the number
  # after it is what is being compared. The tool's whole output is read before it is
  # searched, and the search reads all of it: a reader that leaves at the first match
  # closes the pipe on a tool that is still writing, which dies of SIGPIPE, and under
  # pipefail that ended the whole gate with exit 141.
  have="$("$path" --version 2>&1 || true)"
  have="$(awk '
    !found && match($0, /version [0-9][0-9.]*/) {
      print substr($0, RSTART + 8, RLENGTH - 8)
      found = 1
    }' <<< "$have")"
  if [[ "$have" != "$want" ]]; then
    message="$name is ${have:-of an unknown version} but this repository pins $want (scripts/lint-tools.txt). Install it with: pip install -r scripts/lint-tools.txt"
    if [[ "$STRICT_TOOLS" == "1" ]]; then
      fail "$message"
    fi
    warn "$message"
  fi
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

# ------------------------------------------------------------------- format --
# Runs FIRST, and before the build, because it depends on nothing the build
# produces: it reads the files git already tracks.
#
# It used to run last. The consequence was discovered the hard way: while the
# acceptance suite was red the build failed at link, the gate exited there, and
# the formatting step was never reached — 97 violations accumulated unseen
# behind a failing build. A check that only runs once everything else already
# passes cannot report the state of a repository that does not.
#
# Ordering a static check before the build costs a second and removes that
# whole class of hiding place (ADR-0017).
if [[ "$SKIP_FORMAT" == "0" ]]; then
  step "Checking formatting"
  if CLANG_FORMAT="$(find_tool clang-format)"; then
    check_tool_version clang-format "$CLANG_FORMAT"
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
      # The version is printed because it is not pinned: two machines can
      # disagree about what "formatted" means, and an unreported disagreement
      # is one that gets blamed on the author instead of the toolchain.
      ok "${#files[@]} file(s) correctly formatted ($("$CLANG_FORMAT" --version))"
    fi
  else
    missing_tool clang-format
  fi
else
  warn "formatting check skipped"
fi

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

# --------------------------------------------------------- architectural rules --
# REQ-DET-002, REQ-DEV-001, REQ-DEV-003. A hardware header under core/ is an
# architectural defect, so it fails the build rather than being a review note.
if [[ "$SKIP_ARCH" == "0" ]]; then
  scripts/arch-check.sh || fail "architectural rules violated"
  scripts/header-check.sh || fail "a public header is not self-contained"
else
  warn "architectural rule check skipped"
fi

# ------------------------------------------------------------- determinism --
# REQ-DEV-002: "Repeated runs, including shuffled runs, produce identical
# results." Asserting that in prose proves nothing; running it does.
if [[ "$SKIP_DETERMINISM" == "0" && "$SKIP_TESTS" == "0" ]]; then
  step "Checking test determinism (shuffled and repeated)"
  # The output is kept, not discarded, and shown only if the step fails: with it
  # discarded a failure named no test, and a flake could not be told from a real
  # order dependence. A passing step stays as quiet as it was.
  determinism_log="$(mktemp)"
  if ! ctest --preset "$PRESET" --schedule-random --repeat until-fail:2 \
    --output-on-failure > "$determinism_log" 2>&1; then
    # ctest finishes the whole pass, in random order, before it stops, so a failing
    # test's output can sit anywhere in it with passing tests after it. Show every
    # failing test's own output (the 60 lines after its ***Failed line), then the
    # summary that names them, rather than whatever happens to be last.
    # Either may find nothing (ctest erroring marks no test), and a closed pipe is
    # not an error, so neither may end the script before it says why it is failing.
    { grep -A 60 -F '***' "$determinism_log" | head -n 400; } || true
    sed -n '/^The following tests FAILED/,$p' "$determinism_log" || true
    rm -f "$determinism_log"
    fail "tests are not deterministic under shuffling or repetition (REQ-DEV-002)"
  fi
  rm -f "$determinism_log"
  ok "suite is order-independent and repeatable"
else
  warn "determinism check skipped"
fi

# ------------------------------------------------------------- traceability --
# Runs inside the gate, not only in CI: the Definition of Done requires it, and
# a check that agents must remember to run separately is a check they will
# forget to run.
if [[ "$SKIP_TRACE" == "0" ]]; then
  step "Checking requirement traceability"
  # --tests-passed is the witness that makes "verified" an honest word. It is
  # passed only when this run actually executed the suite and it went green;
  # with --skip-tests nothing was proven, so trace.sh reports citations instead
  # and refuses to write the ratchet.
  #
  # Written as two explicit calls rather than an argument array: macOS ships
  # bash 3.2, where expanding an empty array under `set -u` aborts the script.
  if [[ "$SKIP_TESTS" == "0" ]]; then
    scripts/trace.sh --tests-passed > /dev/null || fail "traceability gaps - run scripts/trace.sh"
  else
    scripts/trace.sh > /dev/null || fail "traceability gaps - run scripts/trace.sh"
  fi
  ok "no coverage regressions; unverified work is reported"
else
  warn "traceability check skipped"
fi

# --------------------------------------------------------- workflow evidence --
# Verifies red-before-green in the commit history (see .github/agents/README.md).
if [[ "$SKIP_WORKFLOW" == "0" ]]; then
  if [[ -n "${PIGEON_COMMIT_RANGE:-}" ]]; then
    scripts/tdd-check.sh --range "$PIGEON_COMMIT_RANGE" || fail "workflow evidence missing"
  else
    scripts/tdd-check.sh || fail "workflow evidence missing"
  fi
else
  warn "workflow evidence check skipped"
fi

# --------------------------------------------------------------------- tidy --
if [[ "$SKIP_TIDY" == "0" ]]; then
  step "Running clang-tidy"
  if CLANG_TIDY="$(find_tool clang-tidy)"; then
    check_tool_version clang-tidy "$CLANG_TIDY"
    if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
      fail "compile_commands.json missing in $BUILD_DIR"
    fi
    read_into_array < <(project_sources | grep -E '\.(cpp|cc)$' | grep -v '^tests/' || true)
    candidate_files=("${read_result[@]:-}")

    # Analyse only what this configuration compiled. A module whose optional
    # dependency is absent (raspberry/ and tools/ without ncnn, ADR-0019 §7) has
    # no compile command, so clang-tidy would guess its flags and fail on
    # headers it was never told about. PIGEON_STRICT_TOOLS=1 makes the
    # dependency mandatory, so in CI nothing is ever skipped here.
    tidy_files=()
    skipped_files=()
    for file in "${candidate_files[@]}"; do
      [[ -z "$file" ]] && continue
      if grep -qF "\"$REPO_ROOT/$file\"" "$BUILD_DIR/compile_commands.json"; then
        tidy_files+=("$file")
      else
        skipped_files+=("$file")
      fi
    done
    if [[ -n "${skipped_files[0]:-}" ]]; then
      warn "${#skipped_files[@]} file(s) not in this build, not analysed: ${skipped_files[*]}"
    fi

    # Homebrew/pip builds of clang-tidy do not know where the macOS SDK is, so
    # the standard library headers are invisible unless we point at it.
    tidy_args=()
    if [[ "$(uname -s)" == "Darwin" ]] && SDK_PATH="$(xcrun --show-sdk-path 2>/dev/null)"; then
      tidy_args+=("--extra-arg=-isysroot" "--extra-arg=$SDK_PATH")
    fi

    if [[ -z "${tidy_files[0]:-}" ]]; then
      ok "no source files to analyse"
    else
      # `${a[@]+"${a[@]}"}`, not `"${a[@]:-}"`: the latter turns an empty array
      # into one empty argument, which clang-tidy reads as a source path and
      # resolves to the working directory. tidy_args is empty off macOS.
      "$CLANG_TIDY" -p "$BUILD_DIR" --quiet ${tidy_args[@]+"${tidy_args[@]}"} "${tidy_files[@]}" \
        || fail "clang-tidy reported issues"
      ok "${#tidy_files[@]} file(s) analysed ($("$CLANG_TIDY" --version | grep -i 'LLVM version' | tr -s ' ' | sed 's/^ *//'))"
    fi
  else
    missing_tool clang-tidy
  fi
else
  warn "clang-tidy skipped"
fi

printf '\n\033[1;32mAll checks passed.\033[0m\n'

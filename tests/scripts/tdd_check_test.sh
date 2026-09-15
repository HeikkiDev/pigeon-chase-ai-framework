#!/usr/bin/env bash
#
# Verifies: infrastructure (acceptance-test-first workflow gate).
#
# The handoff contract in .github/agents/README.md claims that red-before-green
# is "mechanically checkable" from the commit history. This suite proves that
# scripts/tdd-check.sh actually performs that check, by building real git
# histories of each shape and asserting the verdict.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIGEON_REPO_ROOT="${PIGEON_REPO_ROOT:-$(cd "$HERE/../.." && pwd)}"
export PIGEON_REPO_ROOT
# shellcheck source=tests/scripts/support.sh
source "$HERE/support.sh"

TDD_CHECK="$PIGEON_REPO_ROOT/scripts/tdd-check.sh"

echo "tdd-check.sh"

# Deterministic identity and timestamps: the history must be byte-identical on
# every run and on every machine (REQ-DEV-002).
export GIT_AUTHOR_NAME="Gate Test"
export GIT_AUTHOR_EMAIL="gate@example.invalid"
export GIT_COMMITTER_NAME="Gate Test"
export GIT_COMMITTER_EMAIL="gate@example.invalid"
export GIT_AUTHOR_DATE="2026-01-01T00:00:00+00:00"
export GIT_COMMITTER_DATE="2026-01-01T00:00:00+00:00"

# new_history — a git repository with a single base commit on `main`.
new_history() {
  local dir
  dir="$(new_sandbox)"
  git -C "$dir" init --quiet --initial-branch=main
  git -C "$dir" add -A
  git -C "$dir" commit --quiet -m 'chore: baseline' -m 'Refs: infrastructure'
  printf '%s' "$dir"
}

# commit_in <dir> <subject> <body> <file>...
commit_in() {
  local dir="$1" subject="$2" body="$3"
  shift 3
  local file
  for file in "$@"; do
    mkdir -p "$dir/$(dirname "$file")"
    printf '// touched by %s\n' "$subject" >> "$dir/$file"
  done
  git -C "$dir" add -A
  git -C "$dir" commit --quiet -m "$subject" -m "$body"
}

# ------------------------------------------------------- the compliant shape --
history="$(new_history)"
commit_in "$history" 'test(core): specify the confirmation counter' 'Refs: REQ-EXA-001' \
  'tests/core/counter_test.cpp'
commit_in "$history" 'feat(core): implement the confirmation counter' 'Refs: REQ-EXA-001' \
  'core/src/counter.cpp'
expect_exit 'accepts a red commit followed by a green commit' 0 \
  "$TDD_CHECK" --root "$history" --range 'main~2..main'

# ------------------------------------- test and implementation in one commit --
#
# A single mixed commit destroys the evidence: it is impossible to tell whether
# the test ever failed.
history="$(new_history)"
commit_in "$history" 'feat(core): implement and test at once' 'Refs: REQ-EXA-001' \
  'core/src/counter.cpp' 'tests/core/counter_test.cpp'
expect_exit 'rejects a commit that mixes tests with production code' 1 \
  "$TDD_CHECK" --root "$history" --range 'main~1..main'

expect_output_contains 'explains why the mixed commit is rejected' 'tests/core/counter_test.cpp' \
  "$TDD_CHECK" --root "$history" --range 'main~1..main'

# --------------------------------------------- production code with no red step --
history="$(new_history)"
commit_in "$history" 'feat(core): implement the confirmation counter' 'Refs: REQ-EXA-001' \
  'core/src/counter.cpp'
expect_exit 'rejects new behaviour with no preceding test commit' 1 \
  "$TDD_CHECK" --root "$history" --range 'main~1..main'

# ----------------------------------------------------- refactors need no red step --
#
# A refactor changes no behaviour, so it is covered by the tests that already
# exist. Requiring a new failing test would be theatre.
history="$(new_history)"
commit_in "$history" 'refactor(core): extract a helper' 'Refs: REQ-EXA-001' \
  'core/src/counter.cpp'
expect_exit 'permits a refactor with no preceding test commit' 0 \
  "$TDD_CHECK" --root "$history" --range 'main~1..main'

# ------------------------------------------------------ conventional commits ----
history="$(new_history)"
commit_in "$history" 'made the thing work' 'Refs: REQ-EXA-001' 'docs/notes.md'
expect_exit 'rejects a non-conventional commit subject' 1 \
  "$TDD_CHECK" --root "$history" --range 'main~1..main'

history="$(new_history)"
commit_in "$history" 'wibble(core): unknown type' 'Refs: REQ-EXA-001' 'docs/notes.md'
expect_exit 'rejects an unknown commit type' 1 \
  "$TDD_CHECK" --root "$history" --range 'main~1..main'

# ------------------------------------------------------ requirement references ----
history="$(new_history)"
commit_in "$history" 'test(core): specify something' '' 'tests/core/counter_test.cpp'
expect_exit 'rejects a core/ or tests/ commit with no Refs trailer' 1 \
  "$TDD_CHECK" --root "$history" --range 'main~1..main'

history="$(new_history)"
commit_in "$history" 'test(core): specify something' 'Refs: REQ-EXA-001, REQ-EXA-002' \
  'tests/core/counter_test.cpp'
expect_exit 'accepts a Refs trailer naming several requirements' 0 \
  "$TDD_CHECK" --root "$history" --range 'main~1..main'

history="$(new_history)"
commit_in "$history" 'build(ci): wire up the gate' 'Refs: infrastructure' \
  'tests/scripts/support.sh'
expect_exit 'accepts an explicit infrastructure label in place of a requirement' 0 \
  "$TDD_CHECK" --root "$history" --range 'main~1..main'

# ---------------------------------------------- documentation is unconstrained ----
history="$(new_history)"
commit_in "$history" 'docs: describe the workflow' '' 'docs/architecture/architecture.md'
expect_exit 'permits a documentation commit with no requirement reference' 0 \
  "$TDD_CHECK" --root "$history" --range 'main~1..main'

# ------------------------------------------------------------- empty range ------
history="$(new_history)"
expect_exit 'accepts an empty commit range' 0 \
  "$TDD_CHECK" --root "$history" --range 'main..main'

# ------------------------------------------------------------- adoption baseline --
#
# The workflow rules cannot retroactively apply to history written before they
# existed. A baseline commit marks where enforcement begins; everything at or
# before it is out of scope. Without this the gate is unadoptable in any
# repository that already has commits.
history="$(new_history)"
commit_in "$history" 'sloppy commit from before the rules' '' 'core/src/legacy.cpp'
baseline="$(git -C "$history" rev-parse HEAD)"
write_file "$history/.github/workflow-baseline" <<EOF
$baseline
EOF
git -C "$history" add -A
git -C "$history" commit --quiet -m 'ci: adopt the workflow gate' -m 'Refs: infrastructure'
expect_exit 'ignores commits at or before the adoption baseline' 0 \
  "$TDD_CHECK" --root "$history" --range 'main~2..main'

# A baseline must not become a blanket amnesty for everything after it.
commit_in "$history" 'another sloppy one' '' 'core/src/legacy.cpp'
expect_exit 'still rejects violations committed after the baseline' 1 \
  "$TDD_CHECK" --root "$history" --range 'main~3..main'

finish

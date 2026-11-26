#!/usr/bin/env bash
#
# Verifies: infrastructure (the gate-test harness in tests/scripts/support.sh).
#
# Every gate test builds its throwaway trees with new_sandbox(), so two harness
# properties matter to all of them:
#
#   1. Sandboxes are deleted when the test exits. new_sandbox() is always
#      called inside $(...), so anything it records in a shell variable is
#      lost with that subshell; cleanup must not depend on it.
#   2. Git commands run in a sandbox stay in the sandbox. When the gate runs
#      from a git hook, git exports GIT_DIR / GIT_INDEX_FILE and friends, which
#      would otherwise point `git -C <sandbox> ...` at the real repository.
#
# Each property is checked by running a child script that sources support.sh
# exactly as a real gate test does.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIGEON_REPO_ROOT="${PIGEON_REPO_ROOT:-$(cd "$HERE/../.." && pwd)}"
export PIGEON_REPO_ROOT
# shellcheck source=tests/scripts/support.sh
source "$HERE/support.sh"

echo "support.sh"

# child_script <body> — prints the path of a script that sources support.sh,
# runs <body>, and then calls finish() as every gate test does.
child_script() {
  local path
  path="$(new_sandbox)/child.sh"
  {
    printf 'source "%s/support.sh"\n' "$HERE"
    printf '%s\n' "$1"
    printf 'finish\n'
  } > "$path"
  printf '%s' "$path"
}

# --------------------------------------------- sandboxes removed on exit --
scratch="$(new_sandbox)/tmp"
mkdir -p "$scratch"
child="$(child_script 'sandbox="$(new_sandbox)"; [[ -d "$sandbox" ]] || exit 3')"
expect_exit 'a child test creating a sandbox runs cleanly' 0 \
  env TMPDIR="$scratch" bash "$child"
expect_exit 'leaves nothing behind in TMPDIR after it exits' 0 \
  test -z "$(ls -A "$scratch")"

# --------------------------------------- git stays inside the sandbox --
outer="$(new_sandbox)"
git -C "$outer" init -q
child="$(child_script 'sandbox="$(new_sandbox)"
git -C "$sandbox" init -q
git -C "$sandbox" add core')"
expect_exit 'a child test running git in a sandbox under a hook environment runs cleanly' 0 \
  env GIT_DIR="$outer/.git" GIT_WORK_TREE="$outer" \
      GIT_INDEX_FILE="$outer/.git/index" bash "$child"
expect_exit 'does not stage sandbox files in the repository GIT_DIR names' 0 \
  test -z "$(env -u GIT_DIR -u GIT_WORK_TREE -u GIT_INDEX_FILE git -C "$outer" ls-files)"

finish

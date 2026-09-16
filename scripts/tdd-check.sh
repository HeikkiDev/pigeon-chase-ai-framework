#!/usr/bin/env bash
#
# Acceptance-test-first workflow enforcement.
#
# .github/agents/README.md states that red-before-green is visible in the git
# history and therefore checkable. This script performs that check, so the
# claim is evidence rather than etiquette.
#
# For every commit in the range it verifies:
#   1. The subject follows Conventional Commits (AGENTS.md).
#   2. A commit touching core/ or tests/ names the requirements it serves.
#   3. No commit mixes the specification (tests/) with the implementation
#      (core/), because a mixed commit makes the red step unobservable.
#   4. New behaviour (feat/fix in core/) is preceded by a commit that changed
#      tests/ — the failing specification it was written against.
#
# Usage:
#   scripts/tdd-check.sh                       # origin/main..HEAD, or HEAD's branch
#   scripts/tdd-check.sh --range main..HEAD
#   scripts/tdd-check.sh --root DIR --range A..B
#
# Escape hatch: when the specification was committed outside the range under
# inspection, reference it explicitly in the commit body:
#
#   Red: 1a2b3c4
#
# That commit must itself have changed tests/.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RANGE=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --root)  ROOT="$2"; shift 2 ;;
    --range) RANGE="$2"; shift 2 ;;
    -h|--help) sed -n '2,27p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

git_in() { git -C "$ROOT" "$@"; }

if ! git_in rev-parse --git-dir > /dev/null 2>&1; then
  echo "error: not a git repository: $ROOT" >&2
  exit 2
fi

# Default to the commits this branch adds on top of the integration branch.
if [[ -z "$RANGE" ]]; then
  base=""
  for candidate in origin/main main; do
    if git_in rev-parse --verify --quiet "$candidate" > /dev/null; then
      base="$candidate"
      break
    fi
  done
  if [[ -z "$base" ]]; then
    echo "    no integration branch found; nothing to check"
    exit 0
  fi
  RANGE="${base}..HEAD"
fi

readonly CONVENTIONAL='^(feat|fix|test|docs|refactor|build|ci|chore)(\([a-z-]+\))?!?: .+'
readonly REFS='^Refs:[[:space:]]*(REQ-[A-Z]+-[0-9]+([[:space:]]*,[[:space:]]*REQ-[A-Z]+-[0-9]+)*|infrastructure)[[:space:]]*$'

# Adoption baseline. These rules cannot apply retroactively to history written
# before they existed, so .github/workflow-baseline names the commit at which
# enforcement begins. Everything in its ancestry is out of scope; everything
# after it is not.
BASELINE=""
BASELINE_FILE="$ROOT/.github/workflow-baseline"
if [[ -f "$BASELINE_FILE" ]]; then
  candidate="$(grep -oE '^[0-9a-fA-F]{4,40}' "$BASELINE_FILE" | head -1 || true)"
  if [[ -n "$candidate" ]] && git_in rev-parse --verify --quiet "${candidate}^{commit}" > /dev/null; then
    BASELINE="$candidate"
  fi
fi

violations=0
seen_test_commit=0

report() {
  violations=$((violations + 1))
  printf '    violation: %s\n' "$1"
}

files_of() { git_in diff-tree --no-commit-id --name-only -r "$1"; }
subject_of() { git_in show --no-patch --format='%s' "$1"; }
body_of() { git_in show --no-patch --format='%b' "$1"; }

printf '\n\033[1m==> Checking workflow evidence (%s)\033[0m\n' "$RANGE"

if [[ -n "$BASELINE" ]]; then
  printf '    enforcement begins at %s\n' "$(git_in rev-parse --short "$BASELINE")"
  commits="$(git_in rev-list --reverse --no-merges "$RANGE" --not "$BASELINE" 2>/dev/null || true)"
else
  commits="$(git_in rev-list --reverse --no-merges "$RANGE" 2>/dev/null || true)"
fi

if [[ -z "$commits" ]]; then
  printf '\033[32m    ok: no commits to check\033[0m\n'
  exit 0
fi

while IFS= read -r sha; do
  [[ -z "$sha" ]] && continue

  short="$(git_in rev-parse --short "$sha")"
  subject="$(subject_of "$sha")"
  body="$(body_of "$sha")"
  files="$(files_of "$sha")"

  touches_tests=0
  touches_production=0
  touches_core=0

  while IFS= read -r file; do
    [[ -z "$file" ]] && continue
    case "$file" in
      tests/*) touches_tests=1 ;;
      core/src/*|core/include/*) touches_production=1; touches_core=1 ;;
      core/*) touches_core=1 ;;
    esac
  done <<< "$files"

  # 1. Conventional Commits.
  if ! printf '%s' "$subject" | grep -qE "$CONVENTIONAL"; then
    report "$short: subject is not a Conventional Commit: '$subject'"
  fi

  type="${subject%%[(:]*}"

  # 2. Requirement references for anything touching the product or its spec.
  #
  # The trailer is unfolded first. Git folds a long trailer by indenting its
  # continuation lines, and a suite verifying thirty requirements has an honest
  # reason to wrap; rejecting the folded form would reject correct work and
  # quietly pressure authors into citing fewer requirements than they covered.
  # Only indented continuations are joined, so an ordinary following line still
  # terminates the trailer and cannot smuggle anything past the pattern.
  if [[ "$touches_tests" == "1" || "$touches_core" == "1" ]]; then
    unfolded="$(printf '%s\n' "$body" | awk '
      /^Refs:/            { if (pending != "") print pending; pending = $0; next }
      /^[[:space:]]+[^[:space:]]/ { if (pending != "") { line = $0; sub(/^[[:space:]]+/, " ", line); pending = pending line; next } }
                          { if (pending != "") { print pending; pending = "" } }
      END                 { if (pending != "") print pending }
    ')"
    if ! printf '%s\n' "$unfolded" | grep -qE "$REFS"; then
      report "$short: touches core/ or tests/ but has no 'Refs: REQ-...' or 'Refs: infrastructure' trailer"
    fi
  fi

  # 3. The specification and the implementation must not share a commit.
  if [[ "$touches_tests" == "1" && "$touches_production" == "1" ]]; then
    mixed="$(printf '%s\n' "$files" | grep -E '^tests/' | paste -sd ' ' - || true)"
    report "$short: mixes the specification with the implementation, so the red step cannot be observed (${mixed})"
  fi

  # 4. New behaviour requires a preceding red step.
  if [[ "$touches_production" == "1" && ( "$type" == "feat" || "$type" == "fix" ) ]]; then
    if [[ "$seen_test_commit" == "0" ]]; then
      red_sha="$(printf '%s\n' "$body" | sed -n 's/^Red:[[:space:]]*\([0-9a-fA-F]\{4,40\}\).*/\1/p' | head -1)"
      red_ok=0
      if [[ -n "$red_sha" ]] && git_in rev-parse --verify --quiet "${red_sha}^{commit}" > /dev/null; then
        if files_of "$red_sha" | grep -qE '^tests/'; then
          red_ok=1
        fi
      fi
      if [[ "$red_ok" == "0" ]]; then
        report "$short: adds behaviour to core/ with no preceding commit that changed tests/. Write the failing test first, or cite it with a 'Red: <sha>' trailer."
      fi
    fi
  fi

  if [[ "$touches_tests" == "1" ]]; then
    seen_test_commit=1
  fi
done <<< "$commits"

count="$(printf '%s\n' "$commits" | grep -c . || true)"

if [[ "$violations" -gt 0 ]]; then
  printf '\033[31m    FAILED: %d workflow violation(s) across %s commit(s).\033[0m\n' \
    "$violations" "$count" >&2
  exit 1
fi

printf '\033[32m    ok: %s commit(s) show test-first evidence\033[0m\n' "$count"

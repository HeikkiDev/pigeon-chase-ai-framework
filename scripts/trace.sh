#!/usr/bin/env bash
#
# Requirement traceability check.
#
# Maps every requirement in docs/requirements/requirements.md to the tests that
# verify it, and enforces four properties. Each catches a different failure.
#
#   Status field        Requirements carry no status. Being in the document is
#                       what makes a requirement binding, and being verified by
#                       a test is what makes it implemented. Neither fact is
#                       written down, so neither can be misstated. A
#                       reintroduced "**Status:**" line is rejected. Fatal.
#
#   Unknown reference   A test naming a requirement that does not exist traces
#                       to nothing while appearing to trace to something. A
#                       typo in a "Verifies:" comment is invisible otherwise.
#                       Fatal.
#
#   Dangling succession A "**Superseded by:**" pointing at an ID that does not
#                       exist loses the behaviour it claims to have relocated.
#                       Fatal.
#
#   Coverage regression A requirement that was verified and no longer is. This
#                       is what deleting a test to get a green build looks like
#                       from the outside (AGENTS.md rule 14). The verified set
#                       is recorded in docs/requirements/verified.txt and only
#                       ever ratchets upwards. Fatal.
#
# A requirement with no test yet is reported as UNVERIFIED and does not fail
# the gate: nobody has claimed it works, so it is outstanding work rather than
# a false claim. See ADR-0006.
#
# Usage:
#   scripts/trace.sh                    # print the matrix, fail on gaps
#   scripts/trace.sh --report           # print the matrix, never fail
#   scripts/trace.sh --update-baseline  # record the current verified set
#   scripts/trace.sh --root DIR         # check an arbitrary tree (used by tests)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPORT_ONLY=0
UPDATE_BASELINE=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --root)            ROOT="$2"; shift 2 ;;
    --report)          REPORT_ONLY=1; shift ;;
    --update-baseline) UPDATE_BASELINE=1; shift ;;
    -h|--help) sed -n '2,37p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

cd "$ROOT"

REQUIREMENTS_FILE="docs/requirements/requirements.md"
BASELINE_FILE="docs/requirements/verified.txt"

if [[ ! -f "$REQUIREMENTS_FILE" ]]; then
  echo "error: $REQUIREMENTS_FILE not found under $ROOT" >&2
  exit 1
fi

# Markdown fences hold illustrations, not declarations. The requirements
# document has to be able to show the supersession syntax without the gate
# believing a requirement was declared, so strip fenced blocks before parsing.
prose() {
  awk '
    /^[[:space:]]*```/ { fenced = !fenced; next }
    !fenced
  ' "$REQUIREMENTS_FILE"
}

# Emit "<REQ-ID> <SUCCESSOR-OR-DASH>" for every requirement heading.
requirements() {
  prose | awk '
    /^###[[:space:]]+REQ-[A-Z]+-[0-9]+/ {
      match($0, /REQ-[A-Z]+-[0-9]+/)
      if (id != "") print id, successor
      id = substr($0, RSTART, RLENGTH)
      successor = "-"
      next
    }
    /^\*\*Superseded by:\*\*/ {
      if (id != "" && match($0, /REQ-[A-Z]+-[0-9]+/)) {
        successor = substr($0, RSTART, RLENGTH)
      }
      next
    }
    END { if (id != "") print id, successor }
  '
}

referenced_ids() {
  grep -rhoE 'REQ-[A-Z]+-[0-9]+' \
    --include='*.cpp' --include='*.cc' --include='*.hpp' --include='*.h' \
    -- tests 2>/dev/null | LC_ALL=C sort -u || true
}

files_referencing() {
  grep -rl --include='*.cpp' --include='*.cc' --include='*.hpp' --include='*.h' \
    -- "$1" tests 2>/dev/null | LC_ALL=C sort | paste -sd ',' - || true
}

# A status field is a claim, and every claim this gate cannot check is a claim
# someone will eventually make falsely. Reject the field itself.
stale_status=0
stale_status_lines=""
if prose | grep -E '^\*\*Status:\*\*' > /dev/null 2>&1; then
  stale_status="$(prose | grep -cE '^\*\*Status:\*\*')"
  stale_status_lines="$(prose | grep -nE '^\*\*Status:\*\*' | sed 's/^/  /')"
fi

unverified=0
verified_now=""
declared_ids=""
retired_ids=""
successors=""

printf '%-16s %-12s %-22s %s\n' "REQUIREMENT" "VERIFIED" "SUPERSEDED BY" "VERIFIED BY"
printf '%-16s %-12s %-22s %s\n' "---------------" "-----------" "---------------------" "-----------"

total=0
while read -r id successor; do
  [[ -z "$id" ]] && continue
  total=$((total + 1))
  declared_ids="${declared_ids}${id}"$'\n'

  if [[ "$successor" != "-" ]]; then
    retired_ids="${retired_ids}${id}"$'\n'
    successors="${successors}${id} ${successor}"$'\n'
  fi

  refs="$(files_referencing "$id")"

  if [[ -n "$refs" ]]; then
    verified="yes"
    verified_now="${verified_now}${id}"$'\n'
  else
    refs="(none)"
    if [[ "$successor" != "-" ]]; then
      verified="retired"
    else
      verified="UNVERIFIED"
      unverified=$((unverified + 1))
    fi
  fi

  printf '%-16s %-12s %-22s %s\n' "$id" "$verified" "$successor" "$refs"
done < <(requirements)

verified_now="$(printf '%s' "$verified_now" | grep . | LC_ALL=C sort -u || true)"

if [[ "$UPDATE_BASELINE" == "1" ]]; then
  mkdir -p "$(dirname "$BASELINE_FILE")"
  printf '%s\n' "$verified_now" > "$BASELINE_FILE"
  echo
  echo "Recorded $(printf '%s' "$verified_now" | grep -c . || true) verified requirement(s) in $BASELINE_FILE."
  exit 0
fi

# Reverse direction: tests must not cite requirements that do not exist.
unknown=0
unknown_list=""
while IFS= read -r id; do
  [[ -z "$id" ]] && continue
  if ! printf '%s' "$declared_ids" | grep -qx -- "$id"; then
    unknown=$((unknown + 1))
    unknown_list="${unknown_list}  ${id} referenced by: $(files_referencing "$id")"$'\n'
  fi
done < <(referenced_ids)

# A supersession must point at a requirement that actually exists.
dangling=0
dangling_list=""
while read -r id successor; do
  [[ -z "$id" ]] && continue
  if ! printf '%s' "$declared_ids" | grep -qx -- "$successor"; then
    dangling=$((dangling + 1))
    dangling_list="${dangling_list}  ${id} is superseded by ${successor}, which is not declared"$'\n'
  fi
done < <(printf '%s' "$successors" | grep . || true)

# The ratchet: anything in the baseline must still be verified today, unless it
# has been deliberately retired by naming its successor.
regressed=0
regressed_list=""
if [[ -f "$BASELINE_FILE" ]]; then
  while IFS= read -r id; do
    id="${id%%#*}"
    id="$(printf '%s' "$id" | tr -d '[:space:]')"
    [[ -z "$id" ]] && continue
    if ! printf '%s' "$verified_now" | grep -qx -- "$id"; then
      if ! printf '%s' "$retired_ids" | grep -qx -- "$id"; then
        regressed=$((regressed + 1))
        regressed_list="${regressed_list}  ${id}"$'\n'
      fi
    fi
  done < "$BASELINE_FILE"
fi

echo
echo "$total requirement(s) declared, $(printf '%s' "$verified_now" | grep -c . || true) verified by tests."

if [[ "$unverified" -gt 0 ]]; then
  echo "$unverified requirement(s) still UNVERIFIED - outstanding work, not a failure."
fi

failures=$((unknown + dangling + regressed + stale_status))

if [[ "$stale_status" -gt 0 ]]; then
  {
    echo "error: $stale_status requirement(s) carry a '**Status:**' field."
    echo "       Requirements have no status (ADR-0006). Presence in this document"
    echo "       is what makes a requirement binding; a verifying test is what makes"
    echo "       it implemented. Neither is written down, so neither can be misstated."
    echo "       To retire a requirement, use '**Superseded by:** REQ-...' instead."
    printf '%s\n' "$stale_status_lines"
  } >&2
fi

if [[ "$unknown" -gt 0 ]]; then
  {
    echo "error: $unknown requirement ID(s) referenced by tests are not declared in $REQUIREMENTS_FILE:"
    printf '%s' "$unknown_list"
  } >&2
fi

if [[ "$dangling" -gt 0 ]]; then
  {
    echo "error: $dangling dangling supersession(s):"
    printf '%s' "$dangling_list"
    echo "       A retired requirement must name a successor that exists, so the"
    echo "       behaviour it described can still be found."
  } >&2
fi

if [[ "$regressed" -gt 0 ]]; then
  {
    echo "error: COVERAGE REGRESSION - $regressed requirement(s) were verified and no longer are:"
    printf '%s' "$regressed_list"
    echo "       A verified requirement may not quietly become unverified."
    echo "       Restore the test. If the requirement is genuinely retired, add"
    echo "       '**Superseded by:** REQ-...' to it in $REQUIREMENTS_FILE."
  } >&2
fi

if [[ "$failures" -gt 0 ]]; then
  [[ "$REPORT_ONLY" == "1" ]] && exit 0
  exit 1
fi

echo "Traceability OK."

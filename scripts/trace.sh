#!/usr/bin/env bash
#
# Requirement traceability check.
#
# Reads the requirement IDs declared in docs/requirements/requirements.md and
# reports, for each one, which test files reference it. A requirement whose
# status is `Implemented` MUST be referenced by at least one test file;
# otherwise this script fails.
#
# This is the mechanism that turns "the agent says it is done" into evidence.
#
# Usage:
#   scripts/trace.sh            # print the matrix, fail on gaps
#   scripts/trace.sh --report   # print the matrix, never fail

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

REQUIREMENTS_FILE="docs/requirements/requirements.md"
REPORT_ONLY=0

if [[ "${1:-}" == "--report" ]]; then
  REPORT_ONLY=1
fi

if [[ ! -f "$REQUIREMENTS_FILE" ]]; then
  echo "error: $REQUIREMENTS_FILE not found" >&2
  exit 1
fi

# Emit "<REQ-ID> <STATUS>" for every requirement heading in the document.
requirements() {
  awk '
    /^###[[:space:]]+REQ-[A-Z]+-[0-9]+/ {
      match($0, /REQ-[A-Z]+-[0-9]+/)
      if (id != "") print id, status
      id = substr($0, RSTART, RLENGTH)
      status = "Unknown"
      next
    }
    /^\*\*Status:\*\*/ {
      if (id != "") { status = $2; gsub(/[^A-Za-z]/, "", status) }
      next
    }
    END { if (id != "") print id, status }
  ' "$REQUIREMENTS_FILE"
}

printf '%-16s %-14s %s\n' "REQUIREMENT" "STATUS" "VERIFIED BY"
printf '%-16s %-14s %s\n' "---------------" "-------------" "-----------"

failures=0
total=0

while read -r id status; do
  [[ -z "$id" ]] && continue
  total=$((total + 1))

  refs="$(grep -rl --include='*.cpp' --include='*.cc' --include='*.hpp' \
            -- "$id" tests 2>/dev/null | sort | paste -sd ',' - || true)"

  if [[ -z "$refs" ]]; then
    refs="(none)"
  fi

  printf '%-16s %-14s %s\n' "$id" "$status" "$refs"

  if [[ "$status" == "Implemented" && "$refs" == "(none)" ]]; then
    failures=$((failures + 1))
  fi
done < <(requirements)

echo
echo "$total requirement(s) declared."

if [[ "$failures" -gt 0 ]]; then
  echo "error: $failures requirement(s) marked Implemented have no verifying test." >&2
  [[ "$REPORT_ONLY" == "1" ]] && exit 0
  exit 1
fi

echo "Traceability OK."

#!/usr/bin/env bash
#
# Verifies: infrastructure (requirement traceability gate).
#
# scripts/trace.sh is the mechanism that turns "the agent says it is done" into
# evidence, so its failure modes must themselves be proven. A traceability
# checker that cannot fail verifies nothing.
#
# Since ADR-0006 requirements carry no status field at all. Presence in
# requirements.md means the requirement is binding, and implementation status
# is derived from the tests. These cases pin down both of those claims.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIGEON_REPO_ROOT="${PIGEON_REPO_ROOT:-$(cd "$HERE/../.." && pwd)}"
export PIGEON_REPO_ROOT
# shellcheck source=tests/scripts/support.sh
source "$HERE/support.sh"

TRACE="$PIGEON_REPO_ROOT/scripts/trace.sh"

echo "trace.sh"

requirements() {
  write_file "$1/docs/requirements/requirements.md"
}

verified_baseline() {
  write_file "$1/docs/requirements/verified.txt"
}

# -------------------------------------------------------------- nominal case --
sandbox="$(new_sandbox)"
expect_exit "accepts a requirement that has a verifying test" 0 \
  "$TRACE" --root "$sandbox"

# --------------------------------------------------- status fields are gone ----
#
# ADR-0006: a requirement is binding because it is in the document, and
# implemented because a test verifies it. Neither fact is written down, so a
# status field reintroduces a claim the gate cannot check. Reject it outright
# rather than let the old model creep back in.
for status in Draft Approved Implemented Superseded; do
  sandbox="$(new_sandbox)"
  requirements "$sandbox" <<EOF
### REQ-EXA-001 — Example requirement

**Status:** ${status}

**Statement:** Placeholder.
EOF
  expect_exit "rejects a '**Status:** ${status}' field" 1 "$TRACE" --root "$sandbox"
done

sandbox="$(new_sandbox)"
requirements "$sandbox" <<'EOF'
### REQ-EXA-001 — Example requirement

**Status:** Approved

**Statement:** Placeholder.
EOF
expect_output_contains "explains why status fields are rejected" 'ADR-0006' \
  "$TRACE" --root "$sandbox"

# ------------------------------------- a requirement with no test is backlog ---
#
# Presence means binding, so an untested requirement is work not yet done.
# Nobody has claimed it works, so there is nothing to disbelieve: report it,
# do not fail. Otherwise a specification could never be written in advance.
sandbox="$(new_sandbox)"
rm "$sandbox/tests/core/example_test.cpp"
expect_exit "permits a binding requirement that has no verifying test yet" 0 \
  "$TRACE" --root "$sandbox"

expect_output_contains "reports the unverified requirement as outstanding work" 'UNVERIFIED' \
  "$TRACE" --root "$sandbox"

# ------------------------------------------------ unknown requirement reference --
#
# Without this check a typo in a "Verifies:" comment silently produces a test
# that traces to nothing while appearing to trace to something.
sandbox="$(new_sandbox)"
write_file "$sandbox/tests/core/typo_test.cpp" <<'EOF'
// Verifies: REQ-EXA-999
#include <gtest/gtest.h>
EOF
expect_exit "rejects a test referencing a requirement that does not exist" 1 \
  "$TRACE" --root "$sandbox"

expect_output_contains "names the unknown requirement ID" 'REQ-EXA-999' \
  "$TRACE" --root "$sandbox"

# ------------------------------------------------------------ supersession -----
#
# Retirement is content, not state: it names the successor, so the reader
# learns where the behaviour went. IDs are never reused, so the entry stays.
sandbox="$(new_sandbox)"
requirements "$sandbox" <<'EOF'
### REQ-EXA-001 — Example requirement

**Superseded by:** REQ-EXA-002

**Statement:** Placeholder.

### REQ-EXA-002 — Replacement requirement

**Statement:** Placeholder.
EOF
write_file "$sandbox/tests/core/example_test.cpp" <<'EOF'
// Verifies: REQ-EXA-002
#include <gtest/gtest.h>
namespace {
TEST(Example, Works) { EXPECT_TRUE(true); }
}  // namespace
EOF
expect_exit "permits a superseded requirement with no verifying test" 0 \
  "$TRACE" --root "$sandbox"

expect_output_contains "shows the successor in the matrix" 'REQ-EXA-002' \
  "$TRACE" --root "$sandbox"

# A supersession pointing nowhere is a dangling reference, and silently loses
# the behaviour it claims to have relocated.
sandbox="$(new_sandbox)"
rm "$sandbox/tests/core/example_test.cpp"
requirements "$sandbox" <<'EOF'
### REQ-EXA-001 — Example requirement

**Superseded by:** REQ-EXA-404

**Statement:** Placeholder.
EOF
expect_exit "rejects supersession by a requirement that does not exist" 1 \
  "$TRACE" --root "$sandbox"

expect_output_contains "names the dangling successor" 'REQ-EXA-404' \
  "$TRACE" --root "$sandbox"

# ------------------------------------------------------------- the ratchet -----
#
# AGENTS.md rule 14 forbids deleting a test to get a green build, but deleting
# a test file is otherwise silent: the suite simply gets smaller. The verified
# baseline is the ratchet. Once a requirement has been verified, it may never
# quietly become unverified again.
sandbox="$(new_sandbox)"
verified_baseline "$sandbox" <<'EOF'
REQ-EXA-001
EOF
expect_exit "accepts a requirement that is still verified" 0 \
  "$TRACE" --root "$sandbox"

sandbox="$(new_sandbox)"
verified_baseline "$sandbox" <<'EOF'
REQ-EXA-001
EOF
rm "$sandbox/tests/core/example_test.cpp"
expect_exit "rejects a requirement that has lost its verifying test" 1 \
  "$TRACE" --root "$sandbox"

expect_output_contains "names the requirement whose coverage regressed" 'REQ-EXA-001' \
  "$TRACE" --root "$sandbox"

expect_output_contains "calls the regression by its name" 'REGRESSION' \
  "$TRACE" --root "$sandbox"

# Retiring a requirement is a deliberate, visible edit to the specification, so
# the ratchet must honour it rather than demand a test forever.
sandbox="$(new_sandbox)"
verified_baseline "$sandbox" <<'EOF'
REQ-EXA-001
EOF
rm "$sandbox/tests/core/example_test.cpp"
requirements "$sandbox" <<'EOF'
### REQ-EXA-001 — Example requirement

**Superseded by:** REQ-EXA-002

**Statement:** Placeholder.

### REQ-EXA-002 — Replacement requirement

**Statement:** Placeholder.
EOF
expect_exit "permits a superseded requirement to drop out of the verified set" 0 \
  "$TRACE" --root "$sandbox"

# The baseline must be updatable, or it becomes impossible to add coverage.
sandbox="$(new_sandbox)"
expect_exit "--update-baseline records the currently verified requirements" 0 \
  "$TRACE" --root "$sandbox" --update-baseline

if [[ -f "$sandbox/docs/requirements/verified.txt" ]] \
   && grep -qx 'REQ-EXA-001' "$sandbox/docs/requirements/verified.txt"; then
  _ok "the updated baseline contains the verified requirement"
else
  _bad "the updated baseline does not contain the verified requirement"
fi

# ------------------------------------------------------------- --report never fails --
sandbox="$(new_sandbox)"
verified_baseline "$sandbox" <<'EOF'
REQ-EXA-001
EOF
rm "$sandbox/tests/core/example_test.cpp"
expect_exit "--report reports a regression without failing" 0 \
  "$TRACE" --root "$sandbox" --report

# --------------------------------------------- fenced examples are not real ---
#
# The requirements document explains the supersession syntax by showing it in a
# fenced code block. An illustration is not a declaration, and parsing it as one
# invents requirements that nobody wrote.
sandbox="$(new_sandbox)"
requirements "$sandbox" <<'EOF'
### REQ-EXA-001 — Example requirement

**Statement:** Placeholder.

To retire a requirement, name its replacement:

```markdown
### REQ-EXA-777 — Older behaviour

**Superseded by:** REQ-EXA-778
```
EOF
cat > "$sandbox/tests/core/example_test.cpp" <<'EOF'
// Verifies: REQ-EXA-001
TEST(Example, Placeholder) {}
EOF
expect_exit "ignores requirements inside fenced code blocks" 0 \
  "$TRACE" --root "$sandbox"
expect_output_not_contains "does not declare an illustrated requirement" \
  'REQ-EXA-777' "$TRACE" --root "$sandbox"

# ------------------------------------------------------------ the real repository --
expect_exit "the real repository passes its own traceability gate" 0 \
  "$TRACE" --root "$PIGEON_REPO_ROOT"

finish

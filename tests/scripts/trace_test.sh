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
  "$TRACE" --root "$sandbox" --update-baseline --tests-passed

if [[ -f "$sandbox/docs/requirements/verified.txt" ]] \
   && grep -qx 'REQ-EXA-001' "$sandbox/docs/requirements/verified.txt"; then
  _ok "the updated baseline contains the verified requirement"
else
  _bad "the updated baseline does not contain the verified requirement"
fi

# The baseline's header is the only place that says what the file is, how to
# regenerate it, and that entries may never be removed to get a green gate.
# Regenerating the file must not delete its own instructions: a ratchet whose
# rules are erased by the command that maintains it teaches the next reader
# nothing, and the first thing they will reach for is deletion.
sandbox="$(new_sandbox)"
verified_baseline "$sandbox" <<'EOF'
# Verified requirement baseline — the coverage ratchet.
#
# Never remove an entry to make the gate pass.
EOF
"$TRACE" --root "$sandbox" --update-baseline --tests-passed > /dev/null 2>&1
if grep -qF 'Never remove an entry' "$sandbox/docs/requirements/verified.txt" \
   && grep -qx 'REQ-EXA-001' "$sandbox/docs/requirements/verified.txt"; then
  _ok "--update-baseline preserves the baseline's explanatory header"
else
  _bad "--update-baseline destroyed the baseline's explanatory header"
fi

# ------------------------------------------- citing a test is not proving it --
#
# A test file that names a requirement proves nothing until it PASSES. Deriving
# the verified set from the source text alone would let a suite that does not
# even link claim coverage, which is precisely the unearned claim this gate
# exists to prevent. The caller must therefore assert that the tests passed;
# scripts/check.sh does so only after ctest has succeeded.
sandbox="$(new_sandbox)"
expect_output_contains "without --tests-passed the matrix says cited, not verified" \
  'cited by tests' "$TRACE" --root "$sandbox"
expect_output_not_contains "does not claim verification it has not witnessed" \
  'verified by tests' "$TRACE" --root "$sandbox"
expect_output_contains "says how verification is actually witnessed" \
  'check.sh' "$TRACE" --root "$sandbox"

sandbox="$(new_sandbox)"
expect_output_contains "with --tests-passed the matrix says verified" \
  'verified by tests' "$TRACE" --root "$sandbox" --tests-passed

# The ratchet is the record of proven claims, so it may only ever be written
# from a green run. Otherwise a red suite could bake in coverage it never had.
sandbox="$(new_sandbox)"
expect_exit "refuses to update the baseline without evidence the tests passed" 1 \
  "$TRACE" --root "$sandbox" --update-baseline
expect_output_contains "explains why the baseline was not updated" \
  'tests passed' "$TRACE" --root "$sandbox" --update-baseline

sandbox="$(new_sandbox)"
"$TRACE" --root "$sandbox" --update-baseline > /dev/null 2>&1
if [[ -s "$sandbox/docs/requirements/verified.txt" ]] \
   && grep -qx 'REQ-EXA-001' "$sandbox/docs/requirements/verified.txt"; then
  _bad "a refused --update-baseline still wrote the baseline"
else
  _ok "a refused --update-baseline leaves the baseline untouched"
fi

# A coverage regression is a retracted claim and stays fatal either way: the
# requirement was proven once, and nothing has proven it since.
sandbox="$(new_sandbox)"
verified_baseline "$sandbox" <<'EOF'
REQ-EXA-001
EOF
rm "$sandbox/tests/core/example_test.cpp"
expect_exit "a coverage regression is fatal even without --tests-passed" 1 \
  "$TRACE" --root "$sandbox"

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

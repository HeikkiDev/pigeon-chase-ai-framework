# Agent Roles and Workflow

This directory defines the agent roles used to develop the anti-pigeon system.
The roles exist to create **separation of concerns and independent
verification** — the same reasons a human team splits these responsibilities.

The single most important property: **the agent that writes the code is not
the agent that decides whether it works.**

## Roles

Listed in workflow order.

| Agent                     | Owns                                                      | Must not                                             |
| ------------------------- | --------------------------------------------------------- | ---------------------------------------------------- |
| `architect`               | Requirement decomposition, interfaces, ADRs, glossary      | Write implementations or tests                       |
| `test-engineer`           | The executable specification: tests, fixtures, scenarios, traceability | Write production behaviour to make a test pass |
| `implementation-engineer` | Code behind existing interfaces, simulated hardware        | Edit the executable specification, or change requirements or interfaces silently |
| `code-reviewer`           | Independent verification against requirements and safety   | Edit files, or approve without running the gate      |

## Models

Each role declares its model in the `model:` field of its `.agent.md`
frontmatter, so the choice travels with the repository instead of living in
whoever happened to dispatch the agent.

| Agent                     | Model             | Why                                                                     |
| ------------------------- | ----------------- | ----------------------------------------------------------------------- |
| `architect`               | `claude-opus-5`   | Decides what the system must do. Escalating an ambiguity instead of inventing an answer is the behaviour being bought. |
| `test-engineer`           | `claude-opus-5`   | Writes the executable specification. A test that passes vacuously is worse than no test, and spotting that needs judgement. |
| `implementation-engineer` | `claude-opus-5`   | Safety-critical state machine under an exhaustive suite.                |
| `code-reviewer`           | `claude-sonnet-5` | Verifies against explicit `REQ-*` criteria and a gate that returns an exit code. The bar is thoroughness, not invention. |

These are defaults for the role, not for every task. A dispatcher may override
the model for a **mechanical** run — replaying a recipe, a large rename, a
verification pass whose judgement already lives in the prompt — and should,
because those are most of the cheap wins.

Never downgrade the model for work that must not guess: anything touching a
`REQ-SAF-*` requirement, the device protocol, or a question the maintainer has
not ruled on. A weaker model guesses where a stronger one escalates, and this
repository exists to make guessing visible. The saving is not worth a
fabricated requirement.

## Acceptance-test-first (ATDD)

Tests are written **before** the behaviour they verify, and by a different
agent than the one that implements it.

Each `Acceptance` bullet in `docs/requirements/requirements.md` is already
written in an objectively checkable form. The test-engineer's job is to turn
those bullets into **failing tests against the architect's interfaces**. That
failing suite *is* the specification handed to the implementer.

This ordering is deliberate, and it is not classic TDD:

* In classic TDD the implementer writes their own tests. This repository
  forbids that, because it collapses the author and the verifier into one
  agent.
* Writing the test first means it has provably failed for the right reason.
  A test written after a green build has never been observed to fail, and
  "I checked that it can fail" is narration an agent cannot be trusted on.
* Red-before-green is visible in git history, so it is evidence rather than a
  claim. See the handoff contract below.

The implementer may not edit `tests/` to get green. If a test is wrong, it
goes back to the test-engineer with an explanation.

## Standard workflow

```text
        maintainer task
               │
               ▼
        ┌─────────────┐   spec gap?  ──▶ Open Question ──▶ maintainer
        │  architect  │
        └─────────────┘
               │ interfaces + ADR + REQ-* mapping
               ▼
        ┌───────────────┐   acceptance criteria are
        │ test-engineer │   unimplementable? ──▶ back to architect
        └───────────────┘
               │ RED: failing suite committed as the executable spec
               ▼
    ┌───────────────────────┐   test is wrong?
    │ implementation-       │ ──────────────────▶ back to test-engineer
    │ engineer              │   (never edit tests/ to get green)
    └───────────────────────┘
               │ GREEN: gate output, tests/ unmodified
               ▼
        ┌───────────────┐
        │ code-reviewer │ ──▶ Request changes ──┐
        └───────────────┘                       │
               │ Approve                        │
               ▼                                │
            merge  ◀───────────────────────────┘
```

### When to skip steps

* **Trivial change** (typo, comment, docs): straight to `code-reviewer`.
* **No new interface needed**: skip `architect`, but the test-engineer must
  still name the `REQ-*` IDs and go red first.
* **Never skip the red step** for a change to `core/` or to any `REQ-SAF-*`
  behaviour. If there is no failing test, there is no task.
* **Never skip `code-reviewer`** for anything touching `core/` or a
  `REQ-SAF-*` requirement.

## Handoff contract

Each agent's output must let the next agent start without re-deriving context:

| Handoff                                   | Must include                                                                                                                                                  |
| ----------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| architect → test-engineer                 | Interfaces that compile with no behaviour behind them, `REQ-*` mapping, ADR links, what was deliberately left undecided                                          |
| test-engineer → implementation-engineer   | The failing suite, committed as `test(...)`; which requirement and which **Acceptance bullet** each test encodes; the real red output with the failure reason; fixtures added; acceptance bullets deliberately left uncovered, and why |
| implementation-engineer → code-reviewer   | Diff summary, `REQ-*` implemented, real `make check` and `scripts/trace.sh` output, proof `tests/` was not modified (`git diff --stat <red-commit>..HEAD -- tests/`), assumptions made |
| code-reviewer → maintainer                | Verdict, findings by severity, the gate output the reviewer ran themselves, confirmation that red-before-green is visible in the history                          |

### Evidence of the red step

The handback from test-engineer to implementation-engineer is a **commit**, not
a description. Two commits, in this order:

```text
test(core): specify confirmation counter reset      # red, tests/ only
feat(core): confirm targets after three detections  # green, no tests/ changes
```

This makes the ordering mechanically checkable: the test commit must be an
ancestor of the implementation commit, and the implementation commit must not
touch `tests/`. A reviewer who cannot see that shape in the history has not
been given evidence and should request changes.

## Ground rules for every agent

All agents inherit `AGENTS.md`. In particular:

* Run `make check`; never predict its output.
* Never claim done without evidence.
* Never invent requirements — raise an Open Question and stop.
* Never write the implementation before the failing test exists.
* Never weaken a test to get green.
* Escalate ambiguity instead of guessing.

## Adding a new agent

Create `<name>.agent.md` with frontmatter (`name`, `description`, `tools`) and
a body that states, at minimum: what the agent owns, what it must not do, its
method, and the evidence it must produce. Then add it to the tables above.

Keep the role set small. Overlapping agents dilute accountability, which is the
one thing this structure exists to provide.

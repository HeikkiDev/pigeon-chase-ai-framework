# Agent Roles and Workflow

This directory defines the agent roles used to develop the anti-pigeon system.
The roles exist to create **separation of concerns and independent
verification** — the same reasons a human team splits these responsibilities.

The single most important property: **the agent that writes the code is not
the agent that decides whether it works.**

## Roles

| Agent                     | Owns                                                     | Must not                                             |
| ------------------------- | -------------------------------------------------------- | ---------------------------------------------------- |
| `architect`               | Requirement decomposition, interfaces, ADRs, glossary     | Write implementations or tests                       |
| `implementation-engineer` | Code behind existing interfaces, simulated hardware       | Change requirements or interfaces silently           |
| `test-engineer`           | Tests, fixtures, scenarios, traceability                  | Modify production code to make tests pass            |
| `code-reviewer`           | Independent verification against requirements and safety  | Edit files, or approve without running the gate      |

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
    ┌───────────────────────┐         ┌───────────────┐
    │ implementation-       │◀───────▶│ test-engineer │
    │ engineer              │  fix    │               │
    └───────────────────────┘  cycle  └───────────────┘
               │                              │
               └──────────────┬───────────────┘
                              ▼
                      ┌───────────────┐
                      │ code-reviewer │ ──▶ Request changes ──┐
                      └───────────────┘                       │
                              │ Approve                       │
                              ▼                               │
                           merge  ◀──────────────────────────┘
```

### When to skip steps

* **Trivial change** (typo, comment, docs): straight to `code-reviewer`.
* **No new interface needed**: skip `architect`, but the implementer must still
  name the `REQ-*` IDs.
* **Never skip `code-reviewer`** for anything touching `core/` or a
  `REQ-SAF-*` requirement.

## Handoff contract

Each agent's output must let the next agent start without re-deriving context:

| Handoff                | Must include                                                                  |
| ---------------------- | ----------------------------------------------------------------------------- |
| architect → implementer | Interfaces, `REQ-*` mapping, ADR links, what was deliberately left undecided  |
| implementer → tester    | Diff summary, `REQ-*` implemented, `make check` output, assumptions made       |
| tester → reviewer       | Tests added, `REQ-*` verified, proof each test was seen to fail, gate output   |
| reviewer → maintainer   | Verdict, findings by severity, the gate output the reviewer ran themselves     |

## Ground rules for every agent

All agents inherit `AGENTS.md`. In particular:

* Run `make check`; never predict its output.
* Never claim done without evidence.
* Never invent requirements — raise an Open Question and stop.
* Never weaken a test to get green.
* Escalate ambiguity instead of guessing.

## Adding a new agent

Create `<name>.agent.md` with frontmatter (`name`, `description`, `tools`) and
a body that states, at minimum: what the agent owns, what it must not do, its
method, and the evidence it must produce. Then add it to the tables above.

Keep the role set small. Overlapping agents dilute accountability, which is the
one thing this structure exists to provide.

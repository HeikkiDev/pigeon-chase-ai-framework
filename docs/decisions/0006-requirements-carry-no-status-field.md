# ADR-0006 — Requirements carry no status field

**Status:** Accepted
**Date:** 2026-09-15
**Affects:** `docs/requirements/requirements.md`, `scripts/trace.sh`, the agent
workflow
**Supersedes:** the four-state status model introduced with the requirements
document

## Context

Requirements carried a `Status` field with four values: `Draft`, `Approved`,
`Implemented`, `Superseded`. The maintainer asked whether the field was needed
at all.

The first answer given was that `Implemented` should go — it declares a fact
(*is it built?*) that `scripts/trace.sh` already derives from the tests — while
`Draft` and `Approved` should stay, because agreement is a human decision that
nothing in the repository can compute.

That answer was only half consistent, and the maintainer pushed back. Applying
the same test to the values that remained:

* **`Approved` declares a fact that is already recorded.** A requirement
  reaches `main` through a reviewed pull request. The merge *is* the agreement.
  The field restates it in a mutable location that nothing validates.

* **`Draft` is incoherent in this document.** The file describes itself as the
  authoritative specification. An entry that is in the authoritative
  specification but not agreed is a contradiction. Unsettled material already
  has two better homes: the Open Questions table, and an unmerged pull request.

* **The field caused the failure it was supposed to prevent.** All 24
  requirements sat at `Draft`, and the traceability gate only failed on
  requirements marked `Implemented`. The gate was conditioned on the very claim
  it existed to check, so it could not fail for any reason at all. The status
  field did not protect the specification; it concealed that nothing was being
  enforced.

There is also an asymmetry that matters more for agents than for humans. With a
status field, illegitimately promoting a requirement is a **one-word diff** —
`Draft` to `Approved` — easy to miss in review and trivial to produce. Without
one, making something binding requires adding the entire requirement, which is
a conspicuous change to a reviewed document.

One bit does genuinely need recording: **retirement**. IDs must never be reused,
and the coverage ratchet must distinguish a requirement that was deliberately
retired from one whose test was quietly deleted. But that is not a status. It
is content, and `Status: Superseded` discards the useful half of it — *what
replaced this?*

## Decision

**Requirements have no status field.** `scripts/trace.sh` rejects a
`**Status:**` line outright, so the model cannot drift back in.

| Question      | Answered by                                                              |
| ------------- | ------------------------------------------------------------------------- |
| Is it agreed? | Its presence in `requirements.md`, a reviewed document. Presence is binding. |
| Is it built?  | The tests. The gate derives and prints the verified set.                   |
| Is it retired? | A `**Superseded by:** REQ-...` line, which names the successor.           |

Three enforced consequences:

* **A requirement with no test is outstanding work, not a failure.** It is
  reported as `UNVERIFIED`. Nobody has claimed it works, so there is nothing to
  disbelieve, and a specification can still be written before it is built.

* **A requirement that was verified and no longer is fails the gate.** The
  verified set lives in `docs/requirements/verified.txt` and only ratchets
  upwards. This is the enforcement `AGENTS.md` rule 14 never had: deleting a
  test file was previously silent, because the suite merely got smaller.

* **A supersession must name a successor that exists.** A dangling one loses
  the behaviour it claims to have relocated.

The governing asymmetry: **absence of a claim is free; retraction of a proven
claim is expensive.**

## Alternatives considered

| Option                                                       | Why it was rejected                                                                                                                                          |
| ------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Keep all four statuses                                        | The status quo. `Implemented` is a claim nobody checks, and the gate was conditioned on it, so the gate could never fire.                                        |
| Drop `Implemented`, keep `Draft` and `Approved`               | The first answer here, and inconsistent: it removes a declared fact while keeping another declared fact, for no principled reason. Also keeps promotion a one-word diff. |
| Keep `Approved`, validated against the merge history           | The validation reduces to "is this in the document", which is what presence already means. Two representations of one fact, guaranteed to diverge.               |
| Keep `Draft` for work in progress                              | Puts non-binding text in a document defined as authoritative. Open Questions and unmerged PRs already serve this, and neither can be mistaken for a commitment.  |
| `Status: Superseded` rather than `Superseded by:`              | Records that something was retired but not what replaced it, which is the half a reader actually needs. Also leaves the successor unvalidated.                    |
| Delete retired requirements entirely                           | IDs would be reusable, references in old commits and ADRs would dangle, and the ratchet could not tell deliberate retirement from a deleted test.                 |
| Track coverage as a percentage rather than a ratchet           | A percentage can fall while the count rises, and it invites arguing about the threshold. A named set that may not shrink is unambiguous.                          |

## Consequences

### Positive

* Every fact about a requirement is derived from something that cannot be
  edited to lie: the document's contents, the test suite, the git history.
* The traceability table shows honest progress — 24 declared, 0 verified —
  rather than a wall of `Draft` that made the gate vacuous.
* Deleting a test to get green is now caught, at requirement granularity.
* Two manual steps disappear from the agent workflow: updating `Status` to
  `Implemented`, and promoting `Draft` to `Approved`. Both could drift; neither
  exists now.

### Negative / accepted trade-offs

* `verified.txt` is a generated artefact under version control, so concurrent
  branches adding coverage will conflict. The conflicts are trivial — it is a
  sorted list of IDs — but they are real.
* There is no longer a lightweight way to circulate a draft requirement inside
  the document. That is deliberate, and it pushes the discussion into Open
  Questions or a pull request, but it is a genuine loss of convenience.
* The ratchet is at requirement granularity, not per acceptance criterion. A
  requirement with four criteria stays "verified" if three of its tests are
  deleted. Finer granularity needs per-criterion IDs; see ADR-0002 follow-up.
* Someone can still satisfy the ratchet with a trivial test that merely mentions
  the ID in a comment. Mutation testing is the answer, and remains follow-up
  work. This ADR raises the cost of that lie; it does not eliminate it.

## Verification

`tests/scripts/trace_test.sh` covers every rule above, including the cases
where the gate must *not* fire:

```text
    ok: rejects a '**Status:** Draft' field
    ok: rejects a '**Status:** Approved' field
    ok: rejects a '**Status:** Implemented' field
    ok: rejects a '**Status:** Superseded' field
    ok: permits a binding requirement that has no verifying test yet
    ok: reports the unverified requirement as outstanding work
    ok: permits a superseded requirement with no verifying test
    ok: rejects supersession by a requirement that does not exist
    ok: rejects a requirement that has lost its verifying test
    ok: permits a superseded requirement to drop out of the verified set
  23 passed, 0 failed
```

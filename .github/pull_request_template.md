# What changed and why

<!-- One paragraph. What behaviour is different after this PR, and why. -->

## Requirements

<!--
The REQ-* IDs this PR addresses, or an explicit "Infrastructure: <reason>".
Every change is traceable to a requirement or is labelled infrastructure.
-->

- REQ-

## Evidence

<!--
Paste real output. Not a description of the output, not a summary, not a
reconstruction from memory. `make check` runs the architecture, determinism,
traceability and workflow gates as well as the build and tests.
-->

<details>
<summary><code>make check</code></summary>

```text

```

</details>

## Test-first evidence

<!--
The specification commit must come before the implementation commit, and the
implementation must not modify tests/. See .github/agents/README.md.
-->

- Red commit (tests only):
- Green commit (implementation only):
- `git diff --stat <red-commit>..HEAD -- tests/` is empty: yes / no, because …
- Any test here that was retrofitted rather than written red-first, and how its
  failure was demonstrated:

## Assumptions and open questions

<!--
State assumptions explicitly. If you needed an answer that the requirements do
not give, it belongs in the Open Questions table, not in your head.
-->

- Assumptions:
- Open questions raised:

## Checklist

- [ ] Traceable to a `REQ-*` ID, or explicitly labelled infrastructure.
- [ ] New or changed behaviour is covered by a test naming its `REQ-*` ID.
- [ ] `make check` passes and its real output is pasted above.
- [ ] No new compiler warnings (the build is `-Werror`).
- [ ] `core/` still builds and tests with no hardware present.
- [ ] Any architectural decision is recorded as an ADR in `docs/decisions/`.

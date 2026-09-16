# GitHub Copilot Instructions

@../AGENTS.md

**`AGENTS.md` in the repository root is the operating manual and is
authoritative.** If the import above did not resolve, read `AGENTS.md` before
doing anything else. Path-scoped conventions live in
`.github/instructions/`; agent roles live in `.github/agents/`.

## Copilot-specific behaviour

- Prefer repository-local documentation over assumptions. The requirements in
  `docs/requirements/requirements.md` are the specification; do not invent
  behaviour that is not there.
- When a task is ambiguous, inspect the codebase first. If it is still
  ambiguous, add an Open Question to the requirements document and ask — do not
  guess.
- Run `make check` and quote its real output before claiming a task is done.
- Use the custom agents in `.github/agents/` for their respective roles rather
  than doing everything in one pass.

# Claude Code Instructions

@AGENTS.md

**`AGENTS.md` is the operating manual for this repository and is authoritative.**
If the import above did not resolve, read `AGENTS.md` from the repository root
before doing anything else.

## Claude-specific workflow

- Inspect the existing implementation before proposing changes. Read the
  headers you intend to use; never guess an API.
- Use the project's existing tooling: `make check` is the gate. Do not
  introduce alternative build, test or lint tooling.
- For large or architectural changes, produce a plan first and wait for
  approval.
- Use subagents matching the roles in `.github/agents/` — especially, do not
  review your own implementation. Run the `code-reviewer` role against it.
- Never report a task complete without pasting real `make check` output.

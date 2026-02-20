# Agent Instructions

This project uses **bd** (beads) for issue tracking. Run `bd onboard` to get started.

## Quick Reference

```bash
bd ready              # Find available work
bd show <id>          # View issue details
bd update <id> --status in_progress  # Claim work
bd close <id>         # Complete work
bd sync               # Sync with git
```

## Build Policy

**Never offer to build or run `make build`.** The user builds manually and will report back with any errors. Only fix code — do not initiate, background, or wait on builds.

## Git Workflow

**IMPORTANT: Claude does NOT push to remote. User pushes manually.**

### During Development
1. After each working change, **ask the user** if they want to commit
2. If yes, create an intermediate commit with a descriptive message
3. These intermediate commits will be squashed later

## Documentation

When asked to research a subject or topic, save all relevant findings to the `docs/` folder for later reference.

### Rules
- Create `docs/<topic>.md` for new topics (use kebab-case filenames, e.g. `docs/matter-protocol.md`)
- Append to an existing file if the topic is already covered
- Include sources, key concepts, code snippets, and any project-specific notes
- Keep files focused — one topic per file

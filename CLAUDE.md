# Agent Instructions

Use **`TODO.md`** in the project root for all task tracking. Add tasks under the appropriate priority section (`P1`, `P2`, `P3`), mark complete with `[x]`, and remove stale tasks when no longer relevant.

## Build Policy

**Never offer to build or run `make build`.** The user builds manually and will report back with any errors. Only fix code — do not initiate, background, or wait on builds.

## Git Workflow

**IMPORTANT: Claude does NOT push to remote. User pushes manually.**

### During Development
1. After each working change, **ask the user** if they want to commit
2. If yes, create an intermediate commit with a descriptive message
3. These intermediate commits will be squashed later

## Plans

When creating an implementation plan (before writing code), save it in **`docs/<feature>.md`** and add a corresponding task to `TODO.md`.

If the plan changes during implementation, update the docs file.

## Documentation

When asked to research a subject or topic, save all relevant findings to the `docs/` folder for later reference.

### Rules
- Create `docs/<topic>.md` for new topics (use kebab-case filenames, e.g. `docs/matter-protocol.md`)
- Append to an existing file if the topic is already covered
- Include sources, key concepts, code snippets, and any project-specific notes
- Keep files focused — one topic per file

## Landing the Plane (Session Completion)

**When ending a work session**, you MUST complete ALL steps below. Work is NOT complete until `git push` succeeds.

**MANDATORY WORKFLOW:**

1. **Update TODO.md** - Mark completed tasks `[x]`, add any follow-up tasks
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **PUSH TO REMOTE** - This is MANDATORY:
   ```bash
   git pull --rebase
   git push
   git status  # MUST show "up to date with origin"
   ```
5. **Clean up** - Clear stashes, prune remote branches
6. **Verify** - All changes committed AND pushed
7. **Hand off** - Provide context for next session

**CRITICAL RULES:**
- Work is NOT complete until `git push` succeeds
- NEVER stop before pushing - that leaves work stranded locally
- NEVER say "ready to push when you are" - YOU must push
- If push fails, resolve and retry until it succeeds

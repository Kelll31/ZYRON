---
description: Work the next unchecked ROADMAP task (or a given task id) end to end — design, tests first, implement, review, tick the box
argument-hint: "[task id like P2-03 | phase number | empty = next available]"
---

Work on the ZYRON roadmap. Argument: `$ARGUMENTS`

1. **Pick the task.** Read `docs/ROADMAP.md`. If the argument is a task id, use it. If it is a phase number, take the
   first unchecked task in that phase whose `dep:` tasks are all ticked. If empty, take the first unchecked task
   in the lowest unfinished phase with satisfied dependencies, preferring `[MVP1]`. If the task is marked `⛔`,
   or depends on an Open ADR in `docs/DECISIONS.md`, **stop and ask the owner** (or run `prior-art-scout`/the
   named spike) instead of guessing. Say which task you picked and why.
2. **Re-read** the cited `§` in `docs/SPEC.md`, the relevant parts of `docs/ARCHITECTURE.md`, and existing code.
3. **Research & reuse.** If the task introduces a library, algorithm or pattern, run `prior-art-scout` first.
4. **Design.** If it crosses modules, adds a Command, shared state, a thread or a dependency → `core-architect`
   (write the ADR if the decision is hard to reverse).
5. **Tests first.** `test-engineer` writes failing tests (RED); run them and show they fail for the right reason.
6. **Implement** with the right agent (`audio-engine-dev`, `juce-ui-dev`, `library-analysis-dev`,
   `ai-runtime-engineer`, `ai-dj-planner`, `build-ci-engineer`). Run the tests until GREEN, then refactor.
7. **Review in parallel:** `arch-reviewer` always; `rt-safety-reviewer` if anything under `src/Audio/`,
   `src/Recording/` or the audio callback path changed; `ecc:code-reviewer` for general quality. Fix CRITICAL and
   HIGH findings; re-run the tests.
8. **Close out:** tick the box in `docs/ROADMAP.md` (and note anything deferred as a new task), update
   `docs/DECISIONS.md` if you decided something, and propose a conventional commit message. **Do not commit or
   push** unless the owner says so.

Report in Russian: what was done, what was verified (with the commands run and their results), what was *not*
verified (no CI, one OS only, no listening test…), and the next suggested task.

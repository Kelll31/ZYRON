---
description: Realtime-safety audit of the current diff (or given paths) with rt-safety-reviewer, plus the RT lint tripwire
argument-hint: "[paths or git range; empty = uncommitted changes + branch diff vs main]"
allowed-tools: Bash(git diff:*), Bash(git status:*), Bash(git log:*), Bash(python:*), Read, Grep, Glob, Agent
---

Audit audio-thread safety. Scope argument: `$ARGUMENTS`

1. Determine scope: given paths/range, otherwise `git diff` + `git diff main...HEAD`. List changed files under
   `src/Audio/`, `src/Recording/` and any other file that contains `// RT` or a known callback entry point
   (`processBlock`, `process`, `getNextAudioBlock`, `audioDeviceIOCallbackWithContext`). If none, report
   "no audio-path changes" and stop.
2. Run the tripwire on each changed file:
   `python .claude/hooks/rt_lint.py --file <path>` (prints findings; exit 2 if any).
3. Launch the `rt-safety-reviewer` agent with the scope, the changed-function list and the tripwire output.
   Tell it to trace the call graph from the entry points and to check the non-RT side of every queue/snapshot the
   diff touches.
4. Merge both results. Report in Russian, grouped CRITICAL → LOW, each with `file:line`, scenario and fix; end
   with a verdict (Approve / Warn / Block) and what could not be verified (no benchmark, no TSan run, one OS).
   Do not edit code in this command.

---
description: Scaffold a new Command in the Command API — type, validation, RT message, mapping name, AI tool schema, tests
argument-hint: "COMMAND_NAME [short description of fields]"
---

Add a Command to the ZYRON Command API. Request: `$ARGUMENTS`

Every user-visible action must be a Command (SPEC §50, §51). Do this in order:

1. Read `docs/ARCHITECTURE.md §5`, `src/Core/Commands/` (existing commands and conventions), and SPEC §50.
   Check the name doesn't already exist or overlap one that does.
2. Ask `core-architect` for the shape if it isn't trivial (fields, ids not pointers, origin/override behaviour,
   whether it needs an `RtMessage`). Trivial = one deck + one scalar.
3. Create/modify, following existing patterns exactly:
   - the command struct (value type, `UPPER_SNAKE` wire name per §50 e.g. `SET_EQ`) and its entry in the
     `Command` variant;
   - validation (ranges, ids exist, no raw paths from AI — ids only) returning a typed `CommandError`;
   - the AppState update and, if the engine must react, the fixed-size `RtMessage` + audio-thread handler
     (route RT code through `audio-engine-dev`);
   - JSON schema / registry entry used by MIDI mapping files and the AI tool definitions;
   - UI binding point (name only; `juce-ui-dev` implements the control).
4. Tests first (`test-engineer` conventions): validation accepts/rejects table; state transition; RT message round
   trip; serialisation of the wire form; schema matches the struct.
5. Run the tests; run `/rt-audit` if an audio handler changed; update `docs/SPEC.md` command list only if the owner
   approved a spec change; tick/add the ROADMAP task. Don't commit.

# ZYRON (formerly "Mini AI StemDeck")

Native C++ cross-platform DJ application: 2/4 decks, realtime audio engine, stem separation,
local AI, CUDA, MIDI, local library. Later: AI recommendations, set builder, autonomous AI-DJ.
**DJ software first, AI second** — with AI fully disabled this must still be a complete DJ tool.

Owner writes in Russian → **reply in Russian**. Code, comments, identifiers, commit messages: English.

## Source of truth (read on demand, not every session)

| File | What |
|---|---|
| `docs/SPEC.md` | Owner's requirements. `§N` = section number of the original spec; cite it in code review/ADRs. |
| `docs/ARCHITECTURE.md` | Threads, Command flow, state ownership, audio graph, module dependency rules. |
| `docs/ROADMAP.md` | Phases/tasks with IDs (`P2-03`) and `[ ]` boxes. `/phase` works from it. |
| `docs/DECISIONS.md` | ADR log. **Check before picking any library or pattern.** Open ADRs need the owner. |
| `docs/DEV_SETUP.md` | Toolchain per OS and what is/isn't installed on the owner's PC. |
| `docs/AI_MODELS.md` | Candidate AI models (HF repos, sizes, licences, I/O contracts) with card-verified vs unverified status. Policy: ADR-0013. |

Current status: **Phase 0 — planning scaffold only, no code yet.** Update ROADMAP checkboxes as work lands.

## Non-negotiables (violating any of these is a bug, not a style issue)

1. **C++20 baseline**, JUCE + CMake. Python only as an AI-model sidecar behind `StemSeparator`/`AIRuntime` (§2).
2. **Realtime thread never**: allocates, locks, does I/O, logs, touches SQLite/network/AI, throws, or
   waits (§10, §67). Details and the full banned list: `ARCHITECTURE.md#realtime-rules`.
3. **One Command API.** UI, MIDI and AI all issue the same Commands; nobody touches the engine
   directly (§8, §50, §51). AI never runs shell commands; paths are validated (§76).
4. **Core knows nothing about CUDA, OS APIs or JUCE GUI.** OS code lives in `src/Platform/<OS>/`
   behind interfaces; no scattered `#ifdef _WIN32` (§78). GPU code only inside AI backends (§37, §79).
5. **Windows, macOS, Linux are equal.** Never write a Windows-only feature to "port later" (§4).
6. **No new dependency without an ADR**: license, cross-platform, maintenance, justification (§69).
   Project is **open source under AGPLv3** (ADR-0002, owner decision): every new dep must be AGPL-compatible
   (not GPL-2.0-only, not proprietary/non-commercial); prefer permissive/LGPL when quality is equal.
7. **Offline-first.** Network only for updates / model download / version check (§75).
8. **No clicks or pops.** Every user-facing parameter is smoothed; no discontinuities (§21, §88).
9. **Every feature ships with tests** (§70). Core/DSP/Library/Analysis coverage ≥ 80 %.
10. Priorities when they conflict (§88): audio stability > latency > cross-platform > performance >
    modular architecture > GPU acceleration > AI features > UI polish.

## Layout and dependency direction

```
src/
  Core/        Commands, State, Events — std-only. Depends on nothing.
  Audio/       Engine, Deck, Mixer, DSP, Effects, Routing  → Core
  Library/     Database, Scanner, Metadata                 → Core
  Analysis/    BPM, Beatgrid, Key, Energy                  → Core
  Stems/       Demucs, BSRoformer, Cache                   → Core, AI
  AI/          Runtime, Backends, Recommendation, SetBuilder, Transition → Core
  MIDI/        Mapping, Learn                              → Core (Command API only)
  Recording/   writer thread, encoders                     → Core
  UI/          Deck, Mixer, Library, Waveform, Settings, AI → Core (read-only snapshots + Commands)
  Platform/    Windows/ MacOS/ Linux/                      → implements interfaces declared in Core
  Application/ composition root: wires everything, owns lifetimes
tests/  models/ (gitignored weights)  resources/  cmake/
```

Arrows point at what a module may include. UI never includes Audio internals; Audio never includes UI;
Stems/AI never include Audio. `arch-reviewer` enforces this.

## Build (once Phase 1 lands)

```bash
cmake --preset windows-debug          # or macos-debug / linux-debug / *-release
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```

CUDA is optional at build time (`ZYRON_ENABLE_CUDA`); tests must pass on a machine without a GPU.

## Working agreement

Research & reuse first (user's global rule): search GitHub / vendor docs / registries before writing
anything non-trivial → `prior-art-scout`. Mixxx is GPL — study its design, never copy its code.

Flow for a feature: **scout (if new territory) → core-architect design (if it crosses modules or adds a
Command) → tdd-guide/test-engineer RED → implementer agent GREEN → reviewers → update ROADMAP → commit.**

| Agent | Use for |
|---|---|
| `core-architect` | Cross-module design, Command API, state/threading, ADRs. Opus, design only. |
| `audio-engine-dev` | Decks, mixer, DSP, FX, sync, keylock, loops, recording. |
| `rt-safety-reviewer` | **Mandatory before commit on any change under `src/Audio/`, `src/Recording/` or anything called from the audio callback.** |
| `arch-reviewer` | Boundaries, Command API bypass, `#ifdef`, god classes, deps/licenses, AI security. Before every commit. |
| `juce-ui-dev` | Components, waveform rendering, themes, layouts. |
| `library-analysis-dev` | SQLite, scanner, BPM/beatgrid/key/energy/structure analysis, search. |
| `ai-runtime-engineer` | AIRuntime, GPUBackend, GPU scheduler, Demucs/ONNX/LibTorch, stem cache, model manager. |
| `ai-dj-planner` | Phases 6–8: recommendation, set builder, transition planner, LLM adapter. |
| `build-ci-engineer` | CMake, presets, vcpkg, CI matrix, sanitizers, packaging. |
| `test-engineer` | Test strategy, offline-render audio tests, RT allocation guard, integration tests. |
| `prior-art-scout` | Library/algorithm selection research with license verdicts. |

Slash commands: `/phase [id]`, `/build [preset]`, `/rt-audit`, `/new-command NAME`, `/adr TITLE`.
A PostToolUse hook (`.claude/hooks/rt_lint.py`) flags banned constructs inside audio-callback functions
in `src/Audio/**`; it is a tripwire, not a substitute for `rt-safety-reviewer`.

## Definition of done

Builds on all three presets' toolchains (or CI says so) · tests added and green · `rt-safety-reviewer`
clean (if audio-path) · `arch-reviewer` clean · no TODO without a ROADMAP/issue id · ROADMAP box ticked ·
new decisions recorded in `docs/DECISIONS.md` · conventional commit (`feat:`, `fix:` …).

## Known risks to keep in mind

- Licence is **AGPLv3, open source only "for now"** (ADR-0002): going closed/commercial later means buying JUCE,
  replacing GPL deps and relicensing all contributions — don't make that harder than necessary.
- Windows ASIO needs Steinberg's SDK (check its current license); Linux PipeWire support in JUCE
  must be verified — ALSA/JACK are the native backends (SPEC gaps #2).
- Per-stem time-stretching can mean 16 stretchers on 4 decks — budget it (`ARCHITECTURE.md#stems-and-keylock-cost`).
- Demucs in C++ is not drop-in: STFT/iSTFT must live outside the ONNX graph (ADR-0006).

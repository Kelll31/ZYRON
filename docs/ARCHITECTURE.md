# Architecture

Status: **design baseline, implemented for the Windows MVP path** (section 13 describes what the executable actually runs; sections 1-12 are the design and still hold). Items marked *(proposed)* need an ADR/owner OK before code
depends on them. Spec references (`§N`) point to `SPEC.md`.

## 1. Big picture

```
 UI (JUCE msg thread) ─┐
 MIDI (callback thr.) ─┼─► Command API ─► CommandBus ─► Core (validate, update AppState)
 AI planner (worker)  ─┘                                   │
                                                           ├─► RT message queue ─► Audio thread
 Library / Analysis / Stems / AI workers ◄── tasks ◄───────┤
                                                           └─► Events ─► UI observers
 Audio thread ─► telemetry snapshots (lock-free) ─► UI (30 Hz timer)
```

One-way flow of *intent* (Commands) and one-way flow of *facts* (Events, telemetry). Nobody holds a pointer
into the audio engine except the engine's own composition code in `Application/` (§8).

## 2. Modules and allowed includes

| Module | May include | JUCE use (proposed ADR-0003) |
|---|---|---|
| `Core` | std, tiny header-only libs | none |
| `Audio` | Core | `juce_audio_basics`, `juce_dsp` ok; `juce_audio_devices` only in `Audio/Routing` |
| `Library` `Analysis` `Recording` | Core | `juce_core`/formats at the edges only |
| `Stems` `AI` | Core (+`AI` for `Stems`) | none (GPU libs only inside `AI/Backends/*`) |
| `MIDI` | Core | `juce_audio_devices` MIDI I/O only |
| `UI` | Core (snapshots, Commands) | `juce_gui_*` — **only here** |
| `Platform/<OS>` | Core (implements its interfaces) | as needed |
| `Application` | everything | composition root |

Rule of thumb: if a unit test for the module needs a window, an audio device or a GPU, the boundary is wrong.

## 3. Threads (§66) and what each may do

| Thread | Owns | Forbidden |
|---|---|---|
| **Message/UI** (JUCE) | `AppState` (authoritative), UI components, CommandBus dispatch | long work (>~2 ms), blocking I/O |
| **Audio RT callback** | engine graph, decks' play state, DSP state | see §4 below |
| **Library worker** | SQLite writer connection, scanner | touching UI/audio objects |
| **Analysis workers** (N) | analysis tasks (BPM, key, energy, waveform) | writing DB directly → results go through Library worker |
| **AI workers** (1 per GPU + 1 CPU) | model sessions, VRAM | blocking the message thread; unbounded VRAM |
| **Loader thread** | file decode into preallocated buffers; deferred frees from audio thread | — |
| **Recorder writer** | disk writes from lock-free FIFO (not in §66 but required) | — |
| **MIDI callback** | parse → translate via mapping → Command | doing the work itself |

## 4. Realtime rules

Applies to everything reachable from `audioDeviceIOCallbackWithContext` / `getNextAudioBlock` / `processBlock` /
`process` and to any function marked `// RT`.

**Banned on the audio thread:** `new`/`delete`/`malloc`/`free`, container growth (`push_back`, `resize`,
`reserve`, `insert`, `std::string`/`juce::String`/`juce::var`/`juce::Array` mutation), `std::make_unique/
make_shared`, `std::function` (may allocate), `std::mutex`/`lock_guard`/`CriticalSection`/`ScopedLock`/condition
variables, `std::thread`/`async`, file/network/SQLite calls, logging (`DBG`, `std::cout`, `printf`,
`juce::Logger`), `sleep`/`wait`, `throw`, `MessageManager`/`callAsync`, first-use function-local `static`
(guarded init can lock), releasing the last reference of a `shared_ptr` (frees memory).

**Required practice:**
- `juce::ScopedNoDenormals` at the top of each callback.
- Preallocate everything in `prepareToPlay(sampleRate, maxBlockSize)`; assert `numSamples <= maxBlockSize`.
- Cross-thread data: SPSC FIFO (`juce::AbstractFifo`-style), `std::atomic<T>` with `static_assert(is_lock_free)`,
  triple-buffer for snapshots, atomic pointer swap + **deferred free on a non-RT thread** (garbage queue).
- Parameters: atomic target + `SmoothedValue` (5–20 ms ramps); never apply a step change to audio-rate gains,
  filter coefficients, crossfader, loop points without crossfade (§21).
- Sample-accurate events: split the block at event offsets; positions/phase as `int64` samples or `double`.
- Time-bounded: worst-case block time budget measured, not assumed (see ROADMAP P4-04).
- NaN/Inf guard + limiter at master; debug builds assert on non-finite samples.

## 5. Command API (§8, §50, §51)

```cpp
// Core/Commands (shape is illustrative — core-architect owns the final form)
using Command = std::variant<LoadTrack, UnloadTrack, Play, Pause, Cue, Sync, SetBpm, SetPitch, SetKeylock,
                             SetVolume, SetGain, SetEq, SetFilter, LoopIn, LoopOut, LoopExit,
                             SetStemVolume, MuteStem, SoloStem /* … */>;
struct CommandOrigin { enum class Kind { UI, Midi, Ai, Script } kind; std::string sourceId; };
Result<void, CommandError> CommandBus::submit(Command, CommandOrigin);
```

- Commands are plain value types; no pointers to engine objects; `DeckId`, `TrackId`, `StemId` are small ids.
- Validation (ranges, ids exist, paths allowed) happens **once**, in Core, before anything reaches the engine.
- A validated Command is (a) applied to `AppState` on the control thread and (b) translated into an
  `RtMessage` (POD, fixed size) pushed to the audio thread's SPSC queue. The audio thread drains the queue at
  the start of each block. Queue full → drop-newest + counter (never block), surfaced in diagnostics.
- Every Command has: a stable name (used by MIDI mapping files, AI tool schema, logs), a JSON schema
  (AI tool calling / mapping files), a unit test. `/new-command` scaffolds all of these.
- **Origin matters:** user input (UI/MIDI) always overrides AI automation on the same deck/param; the AI
  scheduler yields (pauses its timeline for that target) when it sees a user-origin command there (§8, §58).
- AI never gets a shell, a raw path or a pointer — only Commands (§76). `LoadTrack` takes a `TrackId`, not a path.

## 6. State and telemetry

- `AppState` (decks, mixer, loops, cues, stem state, settings) lives on the control thread; mutated only by the
  Command handler. UI reads immutable snapshots (`shared_ptr<const AppState>` swapped on change) → no locks, no
  tearing.
- Audio thread owns *its own* copy of what it needs (play heads, filter states). It publishes **telemetry**
  (positions, meter levels, loop/cue status, xrun counters) to UI through a lock-free snapshot at ≤ 60 Hz.
- The engine never reads `AppState` directly.

## 7. Audio graph (§11, §43, §44, §45)

```
TrackBuffer(s) ─► StemSource×4 (per deck-load) ─► [stem gains/mute/solo] ─► Channel input
Channel: trim/gain ─► EQ(L/M/H) ─► Filter(HPF/LPF+res) ─► FX slot(s) ─► channel fader ─► crossfader assign
Mixer: Σ channels (crossfader curve applied) ─► Master gain ─► Limiter ─► Master out
       └► Cue bus (pre-fader listen) ─► Headphone out (or split-cue fallback)
Master out ─► Record tap (lock-free FIFO ─► writer thread)
```

- **Stems are independent sources, not a deck property** (§44): a `StemSource` can be routed to any channel, so
  "drums of A + vocals of B" is a routing choice, not a special case.
- FX use a small `Effect` interface (`prepare`, `process`, parameter list) and an `EffectRegistry`; the mixer
  only knows the interface (§23).
- Sample-rate policy: engine runs at the device rate; tracks are resampled at load (offline, high quality)
  rather than in the callback, unless tempo/pitch processing is already resampling.
- Headphone/CUE needs ≥ 4 output channels on one device; otherwise use split-cue (SPEC gap #4).

## 8. Stems and keylock cost

Time-stretching dominates CPU. Naively: 4 decks × 4 stems = **16 stretcher instances**.

- **Fast path:** all four stems of a deck routed to the same channel → apply stem gains/mutes *before* a **single
  stretcher per deck** (gain is linear; smoothed gain changes before the stretcher are inaudible).
- **Split path:** only when stems of one deck are routed to different channels (§44) → per-stem stretchers.
- Keylock OFF: plain resampling (varispeed) — cheap, no stretcher.
- Budget at 128 frames / 48 kHz = 2.67 ms per block: measure worst case for 4 decks (fast path) and for the
  split path before committing to a stretcher (ADR-0005, ROADMAP P4-04).
- Memory: 6-min stereo float32 track ≈ 127 MB; 4 stems ≈ 508 MB per deck (~2 GB for 4 decks). Decide
  RAM-resident vs. memory-mapped stem files (ADR-0007) with a stated minimum-RAM profile.

**As built (ADR-0005, ADR-0018, `Audio/DSP/TimeStretcher`, `KeylockRenderer`, `Deck/DeckPlayer`):** the fast path is
implemented. One Signalsmith Stretch instance per deck serves keylock and key shift; the stems are mixed by `StemMixer`
first. The stretcher runs only when it changes something (tempo != 1.0 with keylock, or a key shift); `playhead` is the
audible position, the stretcher reads `inputLatency + tempo * outputLatency` (about 140 ms at 1.0x) ahead of it, and a seek,
cue, play or loop wrap restarts it aligned to the playhead (about 0.6 ms; `AudioGraph` lets one deck per block do it).
Measured (MSVC Release, 48 kHz): ~0.7 % of one core per stretching deck, 4.3 % for 4 stretching decks with echo + reverb on
every channel, glue and limiter. The split path (per-stem routing) has no stretcher yet: `renderStems` stays varispeed.
Channel signal order is now: trim x gain -> EQ -> filter -> free FX slots -> pre-fader `FxUnit`s -> fader -> post-fader
`FxUnit`s (echo-out tails) -> meter; master: sum -> master gain -> NaN guard -> glue compressor -> limiter.

## 9. Library and analysis (§28–§31)

- SQLite in WAL mode, schema versioned with `PRAGMA user_version` migrations, single writer thread, readers via
  short-lived connections. Never accessed from the audio thread. Large blobs (waveform peaks, stems) are files
  in a cache dir referenced by path + content hash.
- Identity of a track = **content hash** (+ path as a hint) so moved/renamed files keep cues, grids, stems.
- Analysis tasks are idempotent and **versioned** (`analysis_version`): improving an algorithm re-queues
  only stale results. **User edits (beatgrid, cues) are never overwritten by re-analysis** (`source = auto|user`).
- Pipeline (§31): Import → Metadata → Duration → BPM → Beatgrid → Key → Energy → Waveform → Ready, each a task
  with explicit dependencies; failures isolate to the task, the track stays usable.

## 10. AI layer (§32–§42, §74, §79)

```
StemSeparator (iface) ─► DemucsStemSeparator ─► AIRuntime (iface) ─► OnnxRuntime | LibTorch | Sidecar
                                                    │
GpuScheduler ─► GPUBackend (iface) ─► CudaBackend | MetalBackend | CpuBackend
```

- Backends are discovered **dynamically** (NVML/CUDA loaded at runtime): the app must start and work on a
  machine with no NVIDIA driver (§40). Core never links CUDA (§37).
- `GpuScheduler`: one worker per GPU, VRAM budget per job (model + activations + chunk size), back-pressure,
  cancellation, progress events. CPU worker always available. Dispatch uses **priority classes**: *interactive* jobs
  (current/next track analysis, transition planning) prefer GPU A, *background* jobs (library stems, embeddings)
  prefer GPU B; either may spill to the other GPU when it is idle. Round-robin (§36) is the degenerate policy.
- Model I/O contracts differ (44.1 kHz stereo 7.8 s chunks for Demucs; 22.05 kHz mono mel for Beat This!; CQT for
  ChordMini; host STFT for BS-RoFormer) → one tested host-side **`Analysis/Features`** module (resample, STFT/iSTFT,
  mel, CQT) feeds every model; AI backends only run tensors (`docs/AI_MODELS.md`).
- Stem cache key = `hash(content) + modelId + modelVersion + params`; hit → no recompute (§42).
- Model Manager (§74): manifest with id, version, size, sha256, supported backends; verify on import/download;
  warn before loading any model that ships native code (§76).
- A Python sidecar (if ADR-0006 picks it for the prototype) is a **separate process** with a fixed argv, IPC over
  a local pipe/socket, validated paths, no shell.

## 11. Platform layer (§78, §80)

Interfaces declared in Core/Platform headers: `AudioDevice`/`AudioStream` (thin over JUCE), `FileSystem`
(app-data/cache dirs, path validation), `GpuInfo`, `Window`/dialogs where JUCE falls short. Per-OS
implementations in `Platform/<OS>/`. Selected by CMake, not by `#ifdef` in shared code.

## 12. Cross-cutting

- **Errors:** `Result<T,E>`/`std::expected`-style at module boundaries; exceptions only for programmer errors
  outside RT code; RT code never throws.
- **Logging:** non-RT only; RT code increments counters/atomics, the UI/log thread reports them.
- **Config:** JSON/TOML in the OS app-data dir; versioned; defaults baked in.
- **Diagnostics panel:** xrun count, queue-full drops, per-block DSP load, GPU/VRAM, worker queues — built early
  (ROADMAP P1-04/P2-05), it is how the stability priority (§88) stays measurable.

## 13. As built: composition root, telemetry and background services (P11)

Verified against the code on 2026-10-08. `tests/integration/test_app_composition.cpp` builds the same object `Main.cpp` runs.

**Composition root.** `Application/ApplicationComposition` owns, in construction order: `StateStore`, `EventBus`, `CommandBus`,
`LibraryService`, `NeuralModels`, `MasterRecorder`, `AudioEngine`, `StemService`, `AutomixController` (null without a library).
It opens no device and creates no window; `Main.cpp` starts the device and the UI, tests call the engine callback directly.
The engine gets what it must not depend on through `AudioEngine::Services` (std::function): `resolvePath` and `gridFor`
(library), `makeWaveform`, `separateStems`, `trackReady` (stems), `setRecording`. So `Audio` never includes `Library` or `Analysis`.

**Command flow.** UI/MIDI/AI -> `CommandBus::submit` (validate, update `AppState`) -> sinks. The engine is a `CommandSink`
(`AudioEngine::onCommand`): `LoadTrack`/`UnloadTrack` queue work for the `TrackLoader` thread; `SetAudioOutput`/`SetTestTone`
are handled on the engine side; `SeparateStems` and `SetRecording` call the injected services; `Sync` is resolved on the
command thread (grids via `gridFor`, a refusal reason goes out as a `notice`); everything else (Play, Pause, Cue, Seek,
SetPlaybackSpeed, SetLoop, gain/volume/EQ, stem volume/mute/solo/cue, crossfader/curve/assign, master gain, deck cue)
is translated to a POD `RtMessage` and pushed through `CommandBridge` (SPSC queue, drop-newest + `droppedMessages` counter).
The audio callback drains the queue, renders `AudioGraph`, applies the `OutputGate` (click-free master mute) and feeds the
master tap (recorder) and test tone.

**Telemetry views (Core/Audio/EngineView.hpp).** The UI includes only Core:
- `ILiveEngineSource::liveState()` -> `LiveEngineState`: per-deck `hasTrack/isPlaying/positionSec/durationSec/peakL/R/hasStems/playbackSpeed`,
  master peaks, `droppedMessages`, `notice` + `noticeSerial`. Backed by the lock-free snapshot; call from the UI thread only.
- `IDeckLoadSource::loadStatus(deck)` -> `DeckLoadStatus`: load phase (Empty/Loading/Ready/Failed), track id, `generation`,
  failure message, the display `WaveformData`, and stem phase/progress/message.
`MainComponent` polls both at 30 Hz and holds view state only; it keeps no simulated telemetry.

**Background services and threads** (all outside the audio callback):

| Service | Thread | Does |
|---|---|---|
| `TrackLoader` (Audio/Deck) | own worker | decode (WAV, then JUCE codecs, ADR-0016) into `TrackBuffer`, build waveform, swap into the deck, deferred frees |
| `LibraryService` | scan worker + analysis worker + `DatabaseWriter` | SQLite (WAL), incremental folder scan, analysis queue; `TrackAnalyzer` registers the "bpm/beatgrid/key/energy" handlers and decodes each track once |
| `TrackAnalyzer` | runs on the analysis worker | Beat This! (tempo/grid), S-KEY (key), DSP energy and structure -> AI markers, display peaks `.zywv`; DSP fallback when weights are missing |
| `StemService` | one worker, one job at a time | HTDemucs via `NeuralModels` (CPU), stem cache `.zyst` (content hash + model + version), hands stems to `AudioEngine::attachStems` |
| `NeuralModels` | none (mutex-guarded lazy sessions) | locates the models folder (`ZYRON_MODELS_DIR`, app data, next to exe, parents) and shares ONNX sessions; CPU by default (ADR-0015) |
| `AutomixController` | message thread (`juce::Timer`, 10 Hz) | feeds `AutonomousDjLoop` with real deck telemetry and grids; issues Commands; user action on a deck cancels pending AI commands |

Neural models are Windows-only for now (`ZYRON_ENABLE_ONNX`, ADR-0015); elsewhere the analysers use their DSP paths and stems report "runtime unavailable".

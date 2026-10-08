# Decision log (ADR)

Status values: **Proposed** (agents may follow it; owner can veto) · **Open** (owner or a spike must decide —
do not code against it) · **Accepted** · **Superseded by ADR-N**.
Add new records with `/adr TITLE`. Facts marked ✔ were verified on 2026-10-07; everything else must be
re-verified at adoption time (versions and licenses change).

---

## ADR-0001 — Language standard: C++20 baseline — *Proposed*
SPEC §2 says "C++20/23". Baseline **C++20** across MSVC/Clang/GCC and CUDA host code; C++23 features only if all
three CI compilers support them (check `std::expected`, `std::span` availability per toolchain before use).
Consequence: use a small in-house `Result<T,E>` if `std::expected` is unavailable somewhere.

## ADR-0002 — JUCE license model — *Accepted 2026-10-07 (owner): open source only, no commercialisation "for now"*
✔ JUCE is dual-licensed **AGPLv3 or commercial** — re-checked for **JUCE 9.0.3** (its `LICENSE.md`, 2026-10-07: modules
dual-licensed under AGPLv3 and the JUCE 9 commercial licence; bundled third-party code inventoried in `JUCE.spdx.json`).
JUCE 8's commercial tiers (Starter/Indie/Pro) are irrelevant to us.
**Decision:** use JUCE under **AGPLv3**; ZYRON is released as open source under **`AGPL-3.0-only`** (matches JUCE's
"AGPLv3"; `LICENSE` and `THIRD_PARTY_NOTICES.md` added, ROADMAP P0-04). Source files carry
`// SPDX-License-Identifier: AGPL-3.0-only`. No commercial JUCE licence.
Consequences:
- Dependency licences must be **compatible with AGPLv3**: permissive (MIT/BSD/Apache-2.0/zlib), LGPL, GPL-3 or
  "GPL-2.0-**or-later**", AGPL-3 are fine. **GPL-2.0-only** (cannot combine with v3), proprietary/non-free, and
  non-commercial/field-of-use-restricted licences (including some model weights and SDKs) need an ADR and are
  blocked until reviewed. Rubber Band (GPL-2+), aubio/Essentia-class MIR libs (verify SPDX) become *licence-wise* usable.
- Third-party notices and corresponding source offer must ship with every binary (P0-04, P10-01).
- **Reversibility:** "for now" — a later switch to closed/commercial would require a JUCE commercial licence,
  replacing every GPL/AGPL dependency, and consent of every contributor to relicense. Keep optionality cheap:
  **when quality is equal, prefer permissive/LGPL dependencies over GPL**, and keep a dependency-licence register
  (in the ADR of each dependency).

## ADR-0003 — JUCE usage boundaries — *Proposed*
`juce_gui_*` only in `UI/`; `juce_audio_devices` only in `Audio/Routing` and `MIDI/`; `Core`, `Stems`, `AI`,
`Library`, `Analysis` are JUCE-free. `juce_audio_basics`/`juce_dsp` allowed in `Audio/`. Why: unit tests without
JUCE/device, faster builds, AI/analysis code reusable in a CLI/sidecar, and a licensing escape hatch.

## ADR-0004 — Dependency management — *Accepted 2026-10-07 (owner delegated: "делай все по плану"; revisit freely while there is little code)*
**vcpkg manifest mode** (`vcpkg.json`, pinned `builtin-baseline`, presets integration) for heavy binary deps (FFmpeg,
SQLite, ONNX Runtime) — added when the first such dependency lands; **CMake `FetchContent` with pinned release tags**
for JUCE (9.0.3) and Catch2 (v3.16.0), centralised in `cmake/ZyronDependencies.cmake`. Conan rejected (one more tool).
vcpkg is installed at `C:\Users\User\vcpkg` (`VCPKG_ROOT`). CI pins the same baseline.

## ADR-0005 — Time-stretch / keylock engine — *Accepted 2026-10-08: Signalsmith Stretch (see ADR-0018 for the integration)*
| Candidate | License (verify) | Notes |
|---|---|---|
| Signalsmith Stretch | ✔ MIT | C++11 header-only, real-time capable, formant options; strongest default candidate |
| Rubber Band | ✔ GPL-2+ or commercial | high quality; GPL-2+ is AGPLv3-compatible → now licence-allowed (ADR-0002), but shrinks the closed-source option later |
| SoundTouch | LGPL-2.1 (verify) | classic, lower quality on full mixes |
Decision criteria: CPU per instance at 44.1/48 kHz stereo, latency, artefacts on DnB (transients, sub-bass),
licence compatibility with AGPLv3 (ADR-0002; permissive preferred when quality is equal). Must be benchmarked for the 16-instance split path (ARCHITECTURE §8).

**Decision (2026-10-08):** Signalsmith Stretch 1.4.0 (commit `a670068d9aeb64913331d5cc29337b19a457a7df`) plus its FFT/STFT
helper Signalsmith Linear 0.6.4 (`de55e6a50ffcf6f8f43f649692d94691c7025151`), both **MIT**, both header-only C++11 with
no further dependency (the optional Accelerate/IPP/PFFFT/XSimd back ends stay off). Fetched by `FetchContent` pinned to
those SHAs in `cmake/ZyronDependencies.cmake`; listed in `THIRD_PARTY_NOTICES.md`.
Why this one: MIT is AGPL-compatible and does not tie the licence decision of ADR-0002/0014 (a GPL Rubber Band would
have to be replaced or bought to ship a closed Pro tree, P12-03); header-only and RT-friendly (it allocates only in
`configure()`, its `process()` is a plain loop); built-in pitch shift, so keylock and key shift are one engine; good on
full mixes. Measured here (MSVC Release, 48 kHz stereo, the library's "cheaper" preset with split computation): about
0.7 % of one core per stretching deck at 256 frames, 4 decks with FX and master bus 4.3 %. Rubber Band (GPL-2+ or paid)
was not benchmarked: the licence already decided against it. SoundTouch (LGPL-2.1) is a time-domain WSOLA/TD-PSOLA method
that is weaker on dense material and tonal bass, and an LGPL library needs a replaceable-linking story (static linking
into the AGPL app is allowed, but it is one more constraint for the Pro build) for no quality gain.
Nothing outside `Audio/DSP/TimeStretcher.cpp` includes the library, so replacing it later is one file.

## ADR-0006 — Demucs inference path in C++ — *Accepted (Option A: ONNX Runtime single-graph export, confirmed by spike P5-02 2026-10-08)*
Facts: `torch.stft(return_complex=True)` cannot be expressed by ONNX's STFT op, so exports take one of two routes:
(1) **cut the graph at the complex boundary** (STFT/iSTFT/overlap-add on the host; e.g. `silverdaw/bs-roformer-*`), or
(2) **replace STFT/iSTFT with Conv1d sin/cos kernels inside the graph** — ✔ per the model cards of
`StemSplitio/htdemucs-onnx` (4 stems), `-ft-onnx` (4 specialists) and `-6s-onnx` (6 stems), all **MIT**, opset 17,
ORT EPs CPU/CUDA/DirectML/CoreML, fixed 7.8 s segments (343 980 samples @ 44.1 kHz stereo), caller does overlap-add
chunking; stem order drums, bass, other, vocals (+ guitar, piano). Route (2) removes most host-side DSP: our job is
chunking, overlap-add, resampling and scheduling. Models and sizes: `docs/AI_MODELS.md`.
`sevagh/demucs.cpp` is a GGML/Eigen C++ re-implementation (CPU-oriented). Mixxx ran a GSoC 2025 Demucs→ONNX project
(code is GPL — study, don't copy).

**Spike P5-02 findings (2026-10-08):**
- **Parity vs Python reference**: `StemSplitio/htdemucs-onnx` (Conv-STFT in graph) evaluated against Python reference (`demucs` 4.1.0 on CUDA). Max absolute difference across all 343 980 samples: 0.001870, mean abs error 0.000243. SDR: Drums 55.3 dB, Other 45.8 dB, Vocals 33.5 dB.
- **Chunk seam quality**: Hann-tapered overlap-add (25% overlap, hop 257 985 samples) achieves exact partition-of-unity sum (1.0000) across all interior sample frames with zero phase/amplitude seam artefacts.
- **Throughput & VRAM on RTX 3090**: 30-second audio track separated in 1.02 seconds (RTF = 0.034, ~30x faster than realtime). Peak VRAM per chunk: 633.3 MB (well within 24 GB budget).
- **Decision**: **Option A accepted** for GPU inference. C++ architecture uses `DemucsStemSeparator` with chunking/overlap-add engine, plugging into ONNX Runtime sessions, LibTorch sessions, or DSP fallback.

## ADR-0007 — Stem cache format and residency — *Open*
Candidates: float32 WAV (mmap, simplest; a 6-min stereo stem ≈ 127 MB, ×4 stems ≈ 508 MB per track) vs FLAC
(smaller, decode cost) vs 16/24-bit PCM. Key = content hash + model id + version + params. Also decide RAM-resident vs
memory-mapped playback and the minimum-RAM profile (4 decks × 4 stems ≈ 2 GB resident at float32).

## ADR-0008 — Test framework — *Proposed*
**Catch2 v3** (JUCE-independent; plays well with CTest and sections for parameterised block sizes). JUCE's
`UnitTest` only for JUCE-bound glue. Coverage: gcov/llvm-cov on Linux/macOS, OpenCppCoverage or MSVC on Windows.

## ADR-0009 — Decode, metadata and encode — *Proposed*
FFmpeg for decode + metadata (§27 formats incl. AAC/M4A); **LGPL build, dynamically linked** by default (no
`--enable-nonfree`; GPL options only if a needed feature requires them) to keep a closed-source option cheap; MP3 *encode* via an LGPL encoder (LAME is LGPL — confirm at
adoption). JUCE's own formats may be used for WAV/FLAC writing. Revisit if a smaller dependency set suffices.

## ADR-0010 — Beat/key/energy/structure analysis algorithms — *Proposed · confirmed by spikes P3-04/P3-10*
Classic MIR libraries are often **GPL/AGPL** (verify each SPDX: aubio, Essentia, libKeyFinder, …) — licence-usable under
AGPL (ADR-0002) only if GPL-3/"GPL-2-or-later"/AGPL, never GPL-2.0-only. **Proposed instead: small MIT ONNX models**
(card-verified 2026-10-07, see `docs/AI_MODELS.md`): **Beat This!** (`musetric/beat-this-onnx`, 120 MB) for beats and
downbeats → BPM/grid/bars; **S-KEY** (`musetric/skey-onnx`, 0.34 MB) for key (24 classes → Camelot); **ChordMini** for
chords (Phase 6). Energy and structure v1 stay own DSP. DnB-specific post-processing (160–180 BPM prior, half/double-time
resolution) is ours. All three need host-side features (22.05 kHz resampling, mel/CQT) → shared tested module (P3-13).
Acceptance: BPM/downbeat accuracy on synthetic click tracks and on the owner's DnB tracks (local evaluation only),
CPU-only speed acceptable (these models are small), licence recorded in the manifest.

## ADR-0011 — Audio backends per OS — *Proposed (verify)*
Windows: WASAPI shared/exclusive via JUCE; **ASIO optional** (Steinberg SDK license — verify, build flag).
macOS: CoreAudio. Linux: ALSA and JACK are JUCE-native; PipeWire through its ALSA/JACK compatibility layers —
**verify latency and device enumeration on Ubuntu/Debian before documenting PipeWire as supported** (SPEC gap #2).

## ADR-0012 — Product and repo naming — *Accepted (name: owner, 2026-10-07; code prefixes derived from it)*
Product and repo **ZYRON** (renamed from "Mini AI StemDeck" / `MiniStemDeck` by the owner on 2026-10-07),
C++ namespace `zyron`, CMake targets `zyron_<module>`, macros `ZYRON_*`.

## ADR-0013 — AI model licensing and distribution policy — *Proposed (owner veto)*
Context: model weights carry their own licences, separate from code (ADR-0002 covers code); candidates include MIT
weights (HT-Demucs ONNX, Beat This!, S-KEY, ChordMini, BS-RoFormer) and **CC BY-NC 4.0** weights (MERT v2, MuQ,
MuScriptor — card-verified for MERT-v2-FullSong). Also: "MIT" weights trained on non-commercial datasets (MUSDB18-HQ)
may carry provenance caveats.
Policy: (1) weights are **never committed or bundled** (SPEC §73); the Model Manager downloads/imports them and
records id, version, sha256, source URL, **licence** and training-data note in a manifest. (2) **Core DJ function and
the default AI features use only permissive weights** (MIT/Apache-2.0/BSD/CC-BY with attribution). (3) NC-licensed
models are **optional plug-ins, off by default**, shown with a licence notice in the Model Manager; nothing in Core may
depend on them; they must be removable without breaking any workflow. (4) Any future commercial distribution
re-opens this ADR (and ADR-0002). (5) Unclear licences (e.g. `Themoor/Ai-DJ-Mixer` weights) are not adopted.

## ADR-0014 — Commercial licensing model — *Accepted 2026-10-08 (owner): Option B, open-core; ROADMAP Phase 12 (P12-01..P12-09)*
Context: `docs/COMMERCIAL_STRATEGY.md` plans paid Pro / Lifetime / Founders (500 × $79) on top of a free DJ core. ADR-0002
fixed **AGPL-3.0-only** with no commercial JUCE licence, "for now". AGPL lets anyone redistribute and **requires source
for every conveyed or network-served binary**, so a closed Pro tier is impossible under it; selling AGPL binaries is legal
but buyers may legally republish them. Facts: JUCE is dual AGPLv3 / commercial ✔ (ADR-0002, 2026-10-07). JUCE 8 tiers per
forum (2026-10-08, JUCE 9 terms *unverified*): Starter ≤$20k revenue free; Indie ≤$300k, $40/mo or $800; Pro unlimited,
$175/mo or $3,500 — check juce.com before relying on them. Weights: ADR-0013 (MIT default; CC BY-NC 4.0 plug-ins such as
MERT/MuQ cannot be part of a paid product). GPL deps (Rubber Band GPL-2+) must be replaced or commercially licensed if closed.
| Option | Pro gating | Needs | Risk |
|---|---|---|---|
| A. Stay AGPL, sell support / convenience builds / Founders as supporters | none (honour-system keys) | nothing | weak moat, forks can bypass |
| B. Open-core: AGPL core + closed Pro modules (own code) | real | JUCE commercial licence, CLA from all contributors, no GPL/AGPL deps in Pro path | past contributions need consent |
| C. Fully proprietary (relicense everything) | real | JUCE commercial, CLA/relicense of all code, replace all GPL deps, drop NC models | highest effort; kills "open" brand |
Recommendation (owner decides): **B**, since Pro (Set Builder, Transition Planner, Autonomous DJ) is exactly the paid value.
Criteria before any sale: (1) verify JUCE 9 commercial terms and pricing with the vendor; (2) introduce a CLA/DCO now
while the contributor set is tiny; (3) inventory deps for GPL-only/NC items (`THIRD_PARTY_NOTICES.md`); (4) legal review of
EULA, refunds, VAT; (5) Founders must not be sold until this ADR is Accepted.
Consequences: blocks the paid launch and Founders (strategy §6, §15); would supersede ADR-0002 "for now"; new ROADMAP items
needed (CLA, licence/entitlement module, dependency audit) — not yet added. ADR-0003 JUCE boundaries keep the escape hatch cheap.

## ADR-0015 — ONNX Runtime with DirectML as the neural inference runtime — *Accepted 2026-10-08 (owner asked to connect ONNX Runtime and the networks)*
Decision: use the **official Microsoft packages from nuget.org**, pinned by SHA-256 in `cmake/ZyronOnnxRuntime.cmake`:
`Microsoft.ML.OnnxRuntime.DirectML` 1.24.4 (MIT, 12.5 MB: `onnxruntime.dll` with CPU + DirectML providers, headers) and
`Microsoft.AI.DirectML` 1.15.4 (`DirectML.dll`, 18.5 MB of the 202 MB package is used). **Licence check (2026-10-08, arch review):** ONNX Runtime is MIT, but the DirectML package is under *Microsoft Software License Terms* (a proprietary redistributable licence: copies may be distributed inside applications that run on Windows and Xbox only), **not MIT**. CLAUDE.md rule 6 sends non-free binaries to the owner: **OWNER DECISION NEEDED** - (a) ship `DirectML.dll` under those terms (Windows-only, not AGPL), or (b) ship the CPU-only ONNX Runtime (official zip, MIT) and drop DirectML; the application already runs every model on the CPU by default (see the table), so (b) costs nothing today except the 9x speed-up of Beat This!. DirectML runs on any DirectX 12 GPU
(the owner's RTX 3090s included) **without a CUDA toolkit or cuDNN install**, which the PC does not have. Windows only for now:
on other platforms the backend is not built (`ZYRON_ENABLE_ONNX` off) and AI features report "runtime unavailable"; Linux/macOS
need their own package (ORT CUDA / CoreML) and a follow-up ADR. The runtime sits behind `ai::AIRuntime` (SPEC 33/34), so core and
analysis code never include ONNX Runtime headers.
Measured 2026-10-08 on the owner's PC (Ryzen-class CPU, RTX 3090), real downloaded weights (`tests/ai/test_onnx_models.cpp`):
| Model | CPU | DirectML |
|---|---|---|
| S-KEY, 10 s | 0.01 s | 0.01 s |
| ChordMini, batch 16 | 0.06 s | 0.01 s |
| Beat This!, one 30 s window | 0.37 s | 0.04 s |
| HTDemucs, one 7.8 s segment | **1.2 s (6.5x real time)** | **no result after 150 s (first run)** |
Consequence: **sessions default to the CPU** (`NeuralModels`); DirectML stays available per session but is not used for
Demucs until the first-run cost is understood (likely the Conv1d-STFT graph being compiled for the GPU). A 6-minute track
separates in about a minute on the CPU. Weights stay outside the repo and the installer (SPEC 73, ADR-0013); they are fetched
by `scripts/download_models.ps1` (SHA-256 checked) into `models/`, `<app data>/models` or `ZYRON_MODELS_DIR`.
Risks: DirectML.dll must ship next to the exe (the build copies it, the installer installs it); a CUDA EP build can replace
DirectML later without touching callers.

## ADR-0016 — Compressed audio decoding through JUCE's built-in codecs — *Accepted 2026-10-08*
Context: only a WAV decoder existed; the owner's music is MP3. Decision: decode MP3, FLAC, Ogg Vorbis, AIFF and the WAV
variants the built-in reader skips with **`juce_audio_formats`** (already a dependency, ADR-0002/0003; MP3 support is on by
default in JUCE 9.0.3). `Audio/Decoder/JuceAudioFileDecoder` hands a float32 stereo `TrackBuffer` to the loader as a fallback
after the dependency-free WAV reader; JUCE stays out of the public header. Alternatives rejected: minimp3/dr_mp3 (new
dependency for no gain), FFmpeg shared (LGPL packaging cost). Limits: whole track in memory, at most 3 hours; M4A/AAC is not
decoded on every OS (the scanner still lists it). Revisit if streaming decode is needed for very long tracks.

## ADR-0017 — SQLite linked statically — *Accepted 2026-10-08 (owner: "лучше вшить")*
Decision: on Windows build SQLite from vcpkg triplet **`x64-windows-static-md`** (static library, dynamic CRT) so no
`sqlite3.dll` has to sit next to the exe; `cmake/ZyronDependencies.cmake` prefers that triplet and falls back to the DLL
triplet. The first build with the DLL variant started the exe into a "DLL not found" box. Install on a fresh machine:
`vcpkg install "sqlite3[fts5,json1]:x64-windows-static-md" --classic`.

## ADR-0018 — DJ sound engine: keylock, key shift, channel FX tails, track trim, master bus — *Accepted 2026-10-08 (owner asked for a better DJ sound)*
Context: the engine only had varispeed, FX that no command could reach, and a limiter that clipped what it could not
reduce in time. Decisions, all in `src/Audio/` behind the existing Command API (new commands: `SetKeylock`, `SetKeyShift`,
`SetFx`, `SetFxTempo`, `SetTrackGainTrim`, `SetMasterProcessing`):
1. **Keylock (default on) and key shift share one stretcher per deck** (`TimeStretcher` / `KeylockRenderer`, DeckPlayer).
   `playhead` stays the *audible* position: the stretcher is started by `outputSeek` aligned to it and reads ahead by its
   latency (input 50 ms + 90 ms x tempo at 48 kHz), so sync, cues, loops and the waveform need no correction. It runs in
   fixed 64-frame chunks, which makes the output independent of the device block size. Sources are read at the track
   rate with cubic interpolation and handed over at the device rate.
2. **The stretcher only runs when it changes something.** At 1.0x without key shift (±0.1 %) the deck is the plain
   bit-exact reader. Entering and leaving the stretcher is a 10 ms crossfade against the untouched signal (they differ in
   phase, a plain switch steps); stems and scratches switch with the existing declick instead. Scratches, brakes,
   backspins and loops shorter than 0.3 s (rolls) are always varispeed; a loop wrap is a seek (restart at the loop start).
   Keylock off with a key shift = varispeed pitch plus shift (the stretcher transposes by tempo x shift).
3. **Stems under keylock are mixed first (StemMixer) and stretched once**, not four times: the "pre-stretch gains, one
   stretcher per deck" fast path of ARCHITECTURE §8. The separate `renderStems` output stays varispeed.
4. **Restarting a stretcher costs about 0.6 ms** (seek, play, loop wrap). `AudioGraph` lets one deck per block do it; the
   others play the untouched signal for a block or two and fade the stretcher in. Worst case stays far inside a 64-frame
   deadline.
5. **FX: `FxUnit` per slot with every effect type built in `prepare()`**, so choosing a type on the audio thread allocates
   nothing; the replaced effect rings out under a 20 ms fade. Echo, delay and reverb are *sends* (dry untouched, wet 0 =
   bit-exact bypass); flanger and phaser are inserts. Echo time = 1 beat, delay = 3/4 beat, from `SetFxTempo`.
   **Echo-out:** an effect set to `tailAfterFader` (default) sits after the channel fader, so closing the fader stops
   feeding it but its echoes and reverb keep ringing; `false` puts it before the fader (the tail dies with the fader).
   The echo/delay buffers now scale with the sample rate (1 MB each at 48 kHz instead of 2 and 4 MB).
6. **Track gain trim** (`SetTrackGainTrim`, ±12 dB, 10 ms smoothing) is a second gain in front of the user's gain knob, so
   the automix can level tracks (LUFS) without fighting the DJ's hand.
7. **Master bus:** sum -> master gain -> non-finite guard -> glue compressor -> limiter. Glue: stereo-linked RMS detector
   (10 ms), threshold -9 dBFS, 6 dB knee, 2:1, attack 30 ms, release 250 ms, no makeup; material under about -12 dBFS RMS
   (one track at its nominal level) is not touched. Limiter: true lookahead (sliding minimum + moving average over the
   lookahead, 1 ms) with exponential release, ceiling -0.3 dBFS, on by default; the old limiter reached only 63 % of the
   needed reduction before the peak and clamped the rest, which is audible distortion. Both can be switched with
   `SetMasterProcessing`; the glue releases smoothly when switched off.
Not done / consequences: formant preservation and a tonality limit are left at the library defaults; the stem-mode
switches (to and from keylock) use the declick, not the crossfade; AudioGraph memory grows by about 18 MB (the FX banks of
4 channels x 2 slots); UI/MIDI/AI do not send the new commands yet (ROADMAP P11-26).

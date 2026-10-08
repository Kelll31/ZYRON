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

## ADR-0005 — Time-stretch / keylock engine — *Open · spike P3-07*
| Candidate | License (verify) | Notes |
|---|---|---|
| Signalsmith Stretch | ✔ MIT | C++11 header-only, real-time capable, formant options; strongest default candidate |
| Rubber Band | ✔ GPL-2+ or commercial | high quality; GPL-2+ is AGPLv3-compatible → now licence-allowed (ADR-0002), but shrinks the closed-source option later |
| SoundTouch | LGPL-2.1 (verify) | classic, lower quality on full mixes |
Decision criteria: CPU per instance at 44.1/48 kHz stereo, latency, artefacts on DnB (transients, sub-bass),
licence compatibility with AGPLv3 (ADR-0002; permissive preferred when quality is equal). Must be benchmarked for the 16-instance split path (ARCHITECTURE §8).

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

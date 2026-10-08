# AI model matrix (candidates)

Status: **research input; since 2026-10-08 four models are downloaded and three are wired into the app** (see "Current status" below). Compiled 2026-10-07 from (a) the owner's research
notes (secondary source, mixed claims) and (b) our check of the Hugging Face model cards the same day. A card is
the author's claim — ✔ below means "the card says so", not "we measured it". Licensing policy: ADR-0013.
Per-model manifests (id, version, sha256, licence, backends) live with the Model Manager (SPEC §74, ROADMAP P5-08); `models/manifest.json` is written by the download script.

Legend: ✔ card-verified 2026-10-07 · ◻ only in the owner's notes, **unverified** · ⚠ risk (see Notes).
Weights are never committed or bundled (SPEC §73); the Model Manager imports/downloads them.

## Stem separation (Phase 5)

| Candidate (HF repo) | Output | Files / size | Licence (card) | Contract | Status |
|---|---|---|---|---|---|
| `StemSplitio/htdemucs-onnx` | 4 stems: drums, bass, other, vocals | `htdemucs.onnx` 316 MB fp32, `…_fp16weights.onnx` 166 MB, opset 17 | MIT | input `mix` `[1,2,343980]` f32, 44.1 kHz stereo, 7.8 s segment; output `stems` `[1,4,2,343980]`; **STFT/iSTFT inside the graph** (Conv1d sin/cos kernels); caller does overlap-add chunking; EPs: CPU, CUDA, DirectML, CoreML | ✔ ⚠ N1 |
| `StemSplitio/htdemucs-ft-onnx` | same 4 stems, 4 specialist models | 4 × 316 MB | MIT (attribution requested) | one ONNX per stem, 7.8 s segments | ✔ (STFT placement not stated) |
| `StemSplitio/htdemucs-6s-onnx` | drums, bass, other, vocals, **guitar, piano** | 258 MB fp32 / 136 MB fp16 | MIT | output `[1,6,2,343980]`, STFT in graph | ✔ |
| `silverdaw/bs-roformer-rhythm-onnx` | 4 stems | ~257 MB fp16 | MIT (card) | **host-side STFT/iSTFT**: inputs `spec_real/imag` `[1,2,1025,801]`, fixed 8 s chunks; card says *offline, not real-time* | ✔ ⚠ N2 |
| `xycld/BS-RoFormer-ONNX`, `silverdaw/mel-band-roformer-vocals-onnx`, BS-RoFormer SW 6-stem | vocals / 6 stems | — | MIT (per notes) | — | ◻ |

## Analysis (Phases 3, 6)

| Candidate | Task | Size | Licence (card) | Contract | Status |
|---|---|---|---|---|---|
| `musetric/beat-this-onnx` (upstream CPJKU/beat_this, ISMIR 2024) | beat + downbeat → BPM, grid, bars | 182 MB (`beat_this.onnx` 190 649 026 bytes; the card's 120 MB was wrong) | MIT | host computes spectrogram: **22 050 Hz mono**, n_fft 1024, hop 441, periodic Hann, centre/reflect pad, ÷√1024, 128 Slaney mels via `mel-filterbank.bin`, `log1p(1000·x)`; input `spect [windows,513,128]`; outputs `beat`, `downbeat` logits `[windows,513]`; host peak-picks (±3 frames, >0), 50 fps | ✔ |
| `musetric/skey-onnx` (Deezer S-KEY) | global key, 24 classes | 0.34 MB | MIT | input `audio [1,samples]` **22 050 Hz mono, peak-normalised**; HCQT *inside* graph; output `probs[24]`, order in `config.json` | ✔ ⚠ N3 |
| `musetric/chordmini-onnx` (ChordMini) | 170-class chord recognition | 17 MB | MIT | host CQT per `cqt-plan.bin` (22 050 Hz, hop 2048, 144 bins, 24/oct); input `features [16,108,144]`; output `logits [16,108,170]` | ✔ |
| BeatNet, MusicNN, Basic Pitch, MuScriptor | beat/meter, tags, pitch→MIDI, audio→notes | — | BeatNet open-source; MusicNN Apache-2.0; Basic Pitch Apache-2.0; MuScriptor CC BY-NC | PyTorch (no ONNX noted) | ◻ |

## Embeddings / semantic (Phases 6–7, optional)

| Candidate | Task | Licence | Status |
|---|---|---|---|
| `m-a-p/MERT-v2-FullSong` (632 M params; 24 kHz mono, 30–360 s; frame embeddings 1024-d @ 25 Hz, masked-mean pooled) | track embedding → similarity/recommendation | **CC BY-NC 4.0** (non-commercial; derivatives must keep terms); no ONNX stated | ✔ ⚠ N4 |
| `m-a-p/MERT-v2-30s`, `OpenMuQ/MuQ-large`, `OpenMuQ/MuQ-MuLan-large` (music↔text) | embeddings / text search | CC BY-NC 4.0 (per notes) | ◻ ⚠ N4 |
| `laion/larger_clap_music` | audio↔text zero-shot | see card | ◻ |
| `OpenMOSS-Team/MOSS-Music-8B-Instruct` | LLM-style music understanding, ~16+ GB fp16 | Apache-2.0 (per notes) | ◻ — "AI brain", never realtime |
| `Themoor/Ai-DJ-Mixer` | idea/benchmark source only (genre CNN, CLAP, mood, specialists; INT8 ONNX) | unclear per owner's notes | ◻ ⚠ — do not adopt weights |

## Current status (2026-10-08, measured on the owner's PC)

Weights were fetched with `scripts/download_models.ps1` into `models/` (git-ignored). Runtime: ONNX Runtime 1.24.4 + DirectML (ADR-0015).

| Model | File on disk | Status in the app | Measured (`tests/ai/test_onnx_models.cpp`) |
|---|---|---|---|
| HTDemucs (`htdemucs/htdemucs.onnx`, 316 446 953 bytes) | downloaded | **wired**: `StemService`, CPU, stem cache `.zyst` | CPU 1.2 s per 7.8 s segment (6.5x real time); DirectML: no result after 150 s on the first run, so CPU is the default. ~0.5 GB RAM per separated track |
| Beat This! (`beat-this/beat_this.onnx`, 190 649 026 bytes + `mel-filterbank.bin`) | downloaded | **wired**: `TrackAnalyzer` tempo + beat grid, CPU | one 30 s window: CPU 0.37 s, DirectML 0.04 s |
| S-KEY (`skey/skey.onnx`, 338 482 bytes) | downloaded | **wired**: `TrackAnalyzer` key (Camelot), CPU | 10 s: 0.01 s on CPU and DirectML |
| ChordMini (`chordmini/chordnet.onnx`, 17 080 550 bytes) | downloaded | **not wired** (loads and runs in the test; `ChordAnalyzer` is not called by `TrackAnalyzer`) | batch 16: CPU 0.06 s, DirectML 0.01 s |
| HTDemucs FT / 6S, BS-RoFormer, embeddings (MERT/MuQ/CLAP), MOSS-Music, LLM | not downloaded | not wired | - |

No device-choice UI exists: all sessions are created on the CPU (`NeuralModels`). The Model Manager UI and the First-run wizard do not read the real models folder yet. Models are looked up in `ZYRON_MODELS_DIR`, `<app data>/models`, next to the executable, then in parent folders of the executable.

## Notes and risks

- **N1 — Demucs conv-STFT parity.** The single-graph export replaces `torch.stft` with Conv1d kernels. Edge/padding handling
  may differ slightly from the Python reference → ADR-0006 acceptance test (per-stem SI-SDR / max-abs error vs
  `E:\github\automix\.venv`, plus seams between 7.8 s chunks). Chunk length is fixed by the export (343 980 samples).
- **N2 — BS-RoFormer** needs our own host STFT/iSTFT in C++ and is documented as offline-only; a good "HQ" candidate, not the
  default. Training data is MUSDB18-HQ, a dataset with **non-commercial research terms** — "MIT weights" may still carry
  provenance questions (same applies to any MUSDB-trained model); verify before relying on it commercially.
- **N3 — S-KEY** card: "training-data provenance … not documented". Fine for a non-commercial project; note it.
- **N4 — CC BY-NC weights (MERT, MuQ, MuScriptor):** compatible with today's non-commercial AGPL project *if downloaded by
  the user, never bundled*, but they would block any future commercialisation (ADR-0002 is "for now"). Treat as optional,
  default-off plug-ins behind a licence notice; the core DJ workflow must never depend on them (ADR-0013).
- **VRAM figures in the owner's table** (e.g. 2–5 GB for HT-Demucs, 6–12 GB for FT, 16+ GB for MOSS-Music) are engineering
  estimates for an RTX 3090, not measurements. We measure peak VRAM per chunk ourselves (ROADMAP P5-02/P5-04).
- **Shared host-side feature extraction.** Four candidates need CPU-side signal processing with exact numerics: mel
  spectrogram (Beat This!, 22.05 kHz), CQT (ChordMini), STFT/iSTFT (BS-RoFormer), resampling to 22.05/24/44.1 kHz.
  One tested `Analysis/Features` module with golden tests against torchaudio/librosa (available in the `automix` venv)
  serves all of them (ROADMAP P3-13).

## Proposed ZYRON AI core (v1) — to be confirmed by the spikes

```
Stems      : HT-Demucs ONNX (FAST) · HT-Demucs FT (HQ) · HT-Demucs 6S (EXTENDED) · BS-RoFormer (optional HQ)
Analysis   : Beat This! (beats/downbeats→BPM, grid) · S-KEY (key) · ChordMini (harmony) · own DSP (energy, structure v1)
Semantic   : MERT / MuQ / CLAP — optional, off by default (NC licences), Phase 6–7
```

GPU roles (owner's idea, refines SPEC §36): treat the two 3090s as **priority classes**, not just round-robin — GPU A
for interactive jobs (current/next track analysis, transition planning), GPU B for background library work (stems,
embeddings). Scheduler policy, not hard-wired (ARCHITECTURE §10, ROADMAP P5-04).

## Evaluation plan

For each candidate: (1) load with ONNX Runtime CUDA EP in C++ (and CPU), (2) numerical parity vs Python reference on
fixed clips, (3) real-time factor and peak VRAM on one 3090, (4) accuracy on the owner's DnB material (the 19 tracks in
`E:\Music\zyron-test` — local evaluation only, never committed), (5) licence record in the manifest.

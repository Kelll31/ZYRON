# Third-party notices

ZYRON is licensed under **AGPL-3.0-only** (see `LICENSE`; decision: `docs/DECISIONS.md` ADR-0002). This file lists
third-party software that is part of, or distributed with, a ZYRON build. Add an entry **in the same change** that
adds a dependency (`arch-reviewer` flags a missing entry). Model weights are not bundled — see `docs/AI_MODELS.md`.

| Component | Version | Licence | Used for | Distributed in binaries? |
|---|---|---|---|---|
| JUCE | 9.0.3 (planned, P1-02) | AGPL-3.0 (dual-licensed with the JUCE 9 commercial licence; we use the AGPL option) | GUI, audio devices, MIDI, DSP helpers | yes |
| JUCE bundled third-party code | as listed in JUCE's `JUCE.spdx.json` for 9.0.3 | various (see that SBOM) | inside JUCE modules/examples | yes, for the modules we build |
| Catch2 | v3.16.0 (planned, P1-03) | BSL-1.0 | unit tests only | no |
| SQLite3 | 3.53.4#2 | Public domain / blessing | Local database for tracks, playlists, metadata, analysis (SPEC section 28, ROADMAP P3-01) | yes |
| ONNX Runtime (DirectML build) | 1.24.4 | MIT | Neural inference: stems, beat/key analysis (ADR-0015) | yes (Windows) |
| Microsoft DirectML | 1.15.4 | **Microsoft Software License Terms** (proprietary redistributable, Windows/Xbox only; NOT an OSI licence - owner decision pending, ADR-0015) | GPU provider of ONNX Runtime (`DirectML.dll`) | yes (Windows) |
| Signalsmith Stretch | 1.4.0 (`a670068d9aeb64913331d5cc29337b19a457a7df`) | MIT (c) Geraint Luff / Signalsmith Audio Ltd. | Time-stretch and pitch-shift for keylock and key shift (ADR-0005, ADR-0018); header-only | yes |
| Signalsmith Linear | 0.6.4 (`de55e6a50ffcf6f8f43f649692d94691c7025151`) | MIT (c) Signalsmith Audio | FFT/STFT used by Signalsmith Stretch; header-only, built-in FFT (no optional back end) | yes |

Planned (not yet integrated; licences to be verified at adoption): FFmpeg (LGPL build, dynamic).

Model weights are not bundled; the user downloads them with `scripts/download_models.ps1`. In use: HTDemucs ONNX
(`StemSplitio/htdemucs-onnx`), Beat This! (`musetric/beat-this-onnx`), S-KEY (`musetric/skey-onnx`), ChordMini
(`musetric/chordmini-onnx`), all MIT per their model cards (ADR-0013).

Corresponding source for any distributed binary: the tagged commit of this repository plus the pinned dependency
versions in `cmake/ZyronDependencies.cmake` / `vcpkg.json`.

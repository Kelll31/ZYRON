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

Planned (not yet integrated; licences to be verified at adoption): FFmpeg (LGPL build, dynamic),
ONNX Runtime (MIT, verify), a time-stretch engine (ADR-0005), Beat This!/S-KEY/ChordMini ONNX models (MIT per model cards).

Corresponding source for any distributed binary: the tagged commit of this repository plus the pinned dependency
versions in `cmake/ZyronDependencies.cmake` / `vcpkg.json`.

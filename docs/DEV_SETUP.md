# Dev setup

Snapshot of the owner's PC (checked 2026-10-07) and what each OS needs. Windows tooling was installed on 2026-10-07 (task P0-01); the macOS/Linux sections remain a checklist. Version numbers drift: confirm with `winget show` / vendor docs.

## Owner's machine (Windows 11 Pro, build 26300)

| Item | State |
|---|---|
| GPUs | 2 × NVIDIA RTX 3090, 24 GB each; driver reports CUDA 13.4 (UMD) |
| git | installed (2.55) |
| Python | 3.12.10 (system) + `uv` 0.12 |
| FFmpeg | `Gyan.FFmpeg 9.0.2 full_build` (static exe, no shared DLLs) |
| MSVC | **installed (pre-existing):** VS Build Tools 2022 17.14, `cl` 19.44.35229 (MSVC 14.44.35207), Windows SDK 10.0.22621, in `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools` |
| CMake | 4.4.4 (installed 2026-10-07 via winget, `C:\Program Files\CMake\bin`) |
| Ninja | 1.13.2 (winget, `%LOCALAPPDATA%\Microsoft\WinGet\Packages\Ninja-build.Ninja_*`) |
| LLVM | 23.1.3 (winget): `clang`, `clang-format`, `clang-tidy` in `C:\Program Files\LLVM\bin` |
| GitHub CLI | `gh` 2.102.0 (winget); not logged in yet — `gh auth login` is the owner's to do |
| vcpkg | 2026-09-26 in `C:\Users\User\vcpkg`, `VCPKG_ROOT` set (user env), telemetry disabled; ADR-0004 still open |
| CUDA Toolkit (`nvcc`) | **not installed** — deliberately deferred (only needed for our own `.cu` kernels) |

New PATH entries only reach shells started **after** the installs; an already-open terminal needs a restart.
| Reference Python AI env | `E:\github\automix\.venv` — demucs 4.1.0, torch 2.11.0+cu128, CUDA works on both GPUs. Lives inside the `automix` clone: if that folder is deleted, recreate with `uv sync` + `uv pip install --reinstall torch torchaudio --index-url https://download.pytorch.org/whl/cu128` |
| Test music (local only) | `E:\Music\zyron-test` — 19 drum & bass MP3s (246 MB), copied from `automix\music` on 2026-10-07 (SHA-256 verified). Not in git (copyrighted); used for BPM/key/stem evaluation, ROADMAP P3-04/P5-02 |

The `automix` venv is the **reference implementation** for Demucs regression tests (ADR-0006): generate golden
stems there, compare C++ output against them. Run it with `PYTHONUTF8=1 uv run --no-sync …` (plain `uv run`
would re-sync torch back to a CPU build; `torchaudio 2.11` also needs *shared* FFmpeg DLLs).

## Windows (target: MSVC + Ninja)

Gotchas verified in a smoke build (CMake 4.4.4 + Ninja + MSVC 19.44, C++20 `std::span`/concepts/`variant`: OK):
- Ninja needs the MSVC environment: `call ...\BuildTools\VC\Auxiliary\Build\vcvars64.bat` first, and
  `C:\Program Files (x86)\Microsoft Visual Studio\Installer` must be on `PATH` or vcvars fails to find `vswhere`.
  Put this in a `.bat` — in a one-line `cmd /c "a && b"` the `%PATH%` is expanded *before* vcvars runs.
- MSVC reports `__cplusplus == 199711` unless `/Zc:__cplusplus` is set → add it (plus `/permissive- /utf-8`)
  in the presets. CMake's own defaults are fine (`/EHsc`, Release `/O2 /Ob2 /DNDEBUG`).
- If a first configure fails (compiler not found), **delete that build dir**: the cache keeps empty flags forever.

Install commands used / for a fresh machine (Build Tools were already present here, so only the extras ran):
```powershell
winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install Kitware.CMake
winget install Ninja-build.Ninja
winget install LLVM.LLVM          # clang-format, clang-tidy
winget install GitHub.cli
git clone https://github.com/microsoft/vcpkg $env:USERPROFILE\vcpkg; & $env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat
setx VCPKG_ROOT "$env:USERPROFILE\vcpkg"
```

- Run builds from a "x64 Native Tools" environment or let CMake presets call `vcvars` (Ninja needs it).
- **CUDA:** the full Toolkit (`nvcc`) is only needed if we write `.cu` kernels. For ONNX Runtime's CUDA execution
  provider you need the CUDA + cuDNN **runtime** libraries matching the ORT build — check the ORT release notes
  for the exact versions before installing anything. If a toolkit is installed, confirm the supported MSVC
  version in the CUDA release notes (host-compiler support lags behind new VS versions).
- ASIO is optional and needs Steinberg's SDK (ADR-0011).

### Dependencies that need setup on Windows (since the real engine was wired)

- **SQLite** from vcpkg, static: `vcpkg install "sqlite3[fts5,json1]:x64-windows-static-md" --classic` (ADR-0017;
  `cmake/ZyronDependencies.cmake` prefers that triplet, the DLL triplet is only a fallback and leaves `sqlite3.dll` missing at run time).
- **ONNX Runtime 1.24.4 + DirectML 1.15.4** (ADR-0015): the first configure **needs network access** and downloads two
  nuget packages from nuget.org (SHA-256 pinned in `cmake/ZyronOnnxRuntime.cmake`); they are cached and extracted in
  `build/<preset>/_ort`, so later configures of the same build dir work offline. `-DZYRON_ENABLE_ONNX=OFF` skips them.
  The build copies `onnxruntime.dll` and `DirectML.dll` next to the executable. No CUDA toolkit or cuDNN is needed.
- **Model weights** are not in git or the installer: `powershell -ExecutionPolicy Bypass -File scripts\download_models.ps1`
  fetches HTDemucs, Beat This!, S-KEY and ChordMini from Hugging Face into `models\` (asks first, verifies SHA-256, can be
  re-run after an interruption; `-Yes` skips the prompt). Total about 0.5 GB. Without weights the app still runs: tempo and
  key use the DSP detectors and stem separation is unavailable.
- Build: `cmake --preset windows-release`, `cmake --build --preset windows-release`.

### Running the tests

```powershell
ctest --preset windows-release --output-on-failure      # 285 tests at the time of writing
```
- Tests that need weights (`tests/ai/test_onnx_models.cpp`, the stem section of `tests/integration/test_app_composition.cpp`)
  skip when the models are missing. `test_onnx_models.cpp` is compiled with `<repo>/models`; the app and the composition test search
  `ZYRON_MODELS_DIR`, `<app data>/models`, next to the executable and its parent folders (a checkout's `models/` is found that way).
- `ZYRON_TEST_MUSIC_DIR` (optional, e.g. `E:\Music\zyron-test`): `test_app_composition.cpp` then scans that folder, waits for
  the analysis of every track (up to 10 minutes) and checks durations and tempo. Unset = that section is skipped.
- Audio output smoke test on the real device: `ZYRON.exe --audio-selftest`.

## macOS

```bash
xcode-select --install
brew install cmake ninja llvm vcpkg
```
Apple Silicon: Metal/MPS backend later (P5-10); Intel: CPU backend. Notarization needs an Apple Developer account (P10-01).

## Linux (Ubuntu/Debian)

```bash
sudo apt install build-essential cmake ninja-build clang clang-tidy clang-format pkg-config git \
  libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev \
  libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libglu1-mesa-dev mesa-common-dev
```
(JUCE's Linux dependency list — verify against the JUCE docs for the JUCE version in use.)
NVIDIA: proprietary driver + CUDA runtime; without a GPU the CPU backend is used.

## Running the project hooks' self-test

```bash
python -m unittest discover -s .claude/hooks -p "test_*.py"
```

---
description: Configure, build and test ZYRON with a CMake preset (default derived from the OS)
argument-hint: "[preset name, e.g. windows-debug | linux-release]"
allowed-tools: Bash(cmake:*), Bash(ctest:*), Bash(ninja:*), Bash(git status:*), Read, Grep, Glob
---

Build ZYRON. Preset argument: `$ARGUMENTS` (if empty: `windows-debug` on Windows, `macos-debug` on macOS,
`linux-debug` on Linux).

1. Check prerequisites: `cmake --version`, `ninja --version`, a C++ compiler for the platform. If something is
   missing, **stop** and point to `docs/DEV_SETUP.md`; do not install system tools without the owner's OK.
2. If `CMakePresets.json` doesn't exist yet (Phase 1 not done), say so and stop.
3. Run, one at a time, and keep the output tail on failure:
   `cmake --preset <preset>` → `cmake --build --preset <preset>` → `ctest --preset <preset> --output-on-failure`.
4. On a failure, read the *first* error (not the cascade), locate it in source, and report: command, exit code,
   the first error with `file:line`, and a proposed fix. Offer `ecc:build-error-resolver` for mechanical fixes.
5. Report in Russian: preset, tool versions, pass/fail counts, warnings count, and anything not built (CUDA off,
   GPU tests skipped).

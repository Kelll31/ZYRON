// SPDX-License-Identifier: AGPL-3.0-only
//
// Test-binary only. In a Debug build the MSVC runtime answers abort(), a failed assert or a terminate() with a modal
// "Debug Error!" dialog; on a CI agent (or during an unattended run) that blocks the process until the harness
// timeout kills it. This static initialiser turns those dialogs into plain stderr output and a non-zero exit code.
// It is the one place in the repository that is allowed to be compiler-specific outside src/Platform (test plumbing).
#if defined(_MSC_VER)

#include <crtdbg.h>
#include <cstdlib>
#include <initializer_list>

namespace {

struct DisableCrashDialogs {
  DisableCrashDialogs() {
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG  // the _CrtSet* debug-report macros expand to nothing in Release
    for (const int reportType : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
      _CrtSetReportMode(reportType, _CRTDBG_MODE_FILE);
      _CrtSetReportFile(reportType, _CRTDBG_FILE_STDERR);
    }
#endif
  }
};

const DisableCrashDialogs kDisableCrashDialogs;

}  // namespace

#endif

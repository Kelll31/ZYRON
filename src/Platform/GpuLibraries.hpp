// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>
#include <vector>

namespace zyron::platform {

/// File names under which NVIDIA's management library (NVML) may be found on this OS, most specific first. Empty where
/// NVIDIA GPUs are not supported (macOS). Defined once per OS in src/Platform; the composition root hands the result to
/// the NVML probe, so neither Core nor the AI module has to know vendor- or OS-specific file names.
[[nodiscard]] std::vector<std::string> nvmlLibraryNames();

}  // namespace zyron::platform

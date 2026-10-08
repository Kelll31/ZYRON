// SPDX-License-Identifier: AGPL-3.0-only
#include "Platform/GpuLibraries.hpp"

namespace zyron::platform {

std::vector<std::string> nvmlLibraryNames() {
  // macOS has no NVIDIA driver; GPU work goes through Metal (SPEC section 38, ROADMAP P5-10).
  return {};
}

}  // namespace zyron::platform

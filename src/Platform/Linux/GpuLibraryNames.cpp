// SPDX-License-Identifier: AGPL-3.0-only
#include "Platform/GpuLibraries.hpp"

namespace zyron::platform {

std::vector<std::string> nvmlLibraryNames() {
  // The driver package installs the versioned name; the unversioned one exists only with dev packages.
  return {"libnvidia-ml.so.1", "libnvidia-ml.so"};
}

}  // namespace zyron::platform

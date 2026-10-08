// SPDX-License-Identifier: AGPL-3.0-only
#include "Core/System/PlatformWindow.hpp"

#include <cstdlib>
#include <memory>

namespace zyron::platform {

class LinuxPlatformWindow final : public core::PlatformWindow {
 public:
  ~LinuxPlatformWindow() override = default;

  [[nodiscard]] double displayScaleFactor() const override {
    const char* gdkScale = std::getenv("GDK_SCALE");
    if (gdkScale != nullptr && *gdkScale != '\0') {
      const double scale = std::atof(gdkScale);
      if (scale > 0.0) {
        return scale;
      }
    }
    return 1.0;
  }

  [[nodiscard]] bool isSystemDarkMode() const override {
    return true;  // Default dark appearance
  }
};

}  // namespace zyron::platform

namespace zyron::core {

std::unique_ptr<PlatformWindow> createPlatformWindow() {
  return std::make_unique<platform::LinuxPlatformWindow>();
}

}  // namespace zyron::core

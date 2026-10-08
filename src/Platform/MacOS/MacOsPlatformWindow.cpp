// SPDX-License-Identifier: AGPL-3.0-only
#include "Core/System/PlatformWindow.hpp"

#include <memory>

namespace zyron::platform {

class MacOsPlatformWindow final : public core::PlatformWindow {
 public:
  ~MacOsPlatformWindow() override = default;

  [[nodiscard]] double displayScaleFactor() const override {
    // macOS Retina displays are 2x by default
    return 2.0;
  }

  [[nodiscard]] bool isSystemDarkMode() const override {
    return true;  // Default dark appearance
  }
};

}  // namespace zyron::platform

namespace zyron::core {

std::unique_ptr<PlatformWindow> createPlatformWindow() {
  return std::make_unique<platform::MacOsPlatformWindow>();
}

}  // namespace zyron::core

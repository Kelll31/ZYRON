// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <memory>

namespace zyron::core {

/// Platform window and display interface (SPEC section 78).
/// Pure interface implemented per OS in src/Platform.
class PlatformWindow {
 public:
  virtual ~PlatformWindow() = default;

  /// Display scale factor (DPI / 96.0 on Windows, Retina scaling on macOS).
  [[nodiscard]] virtual double displayScaleFactor() const = 0;

  /// Whether the host operating system has system-wide dark mode active.
  [[nodiscard]] virtual bool isSystemDarkMode() const = 0;
};

/// Factory declared in Core, implemented in Platform.
[[nodiscard]] std::unique_ptr<PlatformWindow> createPlatformWindow();

}  // namespace zyron::core

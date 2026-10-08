// SPDX-License-Identifier: AGPL-3.0-only
#include "Core/System/PlatformWindow.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <memory>

namespace zyron::platform {

class WindowsPlatformWindow final : public core::PlatformWindow {
 public:
  ~WindowsPlatformWindow() override = default;

  [[nodiscard]] double displayScaleFactor() const override {
    // Windows 10 build 1607 and later support GetDpiForSystem
    const UINT dpi = GetDpiForSystem();
    if (dpi > 0) {
      return static_cast<double>(dpi) / 96.0;
    }
    return 1.0;
  }

  [[nodiscard]] bool isSystemDarkMode() const override {
    // Query AppsUseLightTheme in Windows Registry
    HKEY key = nullptr;
    const LONG res = RegOpenKeyExW(
        HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &key);
    if (res != ERROR_SUCCESS) {
      return true;  // Default to dark mode for DJ software
    }

    DWORD value = 0;
    DWORD size = sizeof(value);
    DWORD type = 0;
    const LONG queryRes =
        RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type, reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);

    if (queryRes == ERROR_SUCCESS && type == REG_DWORD) {
      return value == 0;  // 0 means dark mode, 1 means light mode
    }

    return true;
  }
};

}  // namespace zyron::platform

namespace zyron::core {

std::unique_ptr<PlatformWindow> createPlatformWindow() {
  return std::make_unique<platform::WindowsPlatformWindow>();
}

}  // namespace zyron::core

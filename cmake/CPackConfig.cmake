# SPDX-License-Identifier: AGPL-3.0-only
#
# CPack packaging configuration for ZYRON (SPEC section 72, ROADMAP P10-01).
# Targets:
#   - Windows: .exe (NSIS installer), .msi (WiX installer), .zip
#   - macOS:   .dmg (DragNDrop disk image), .app bundle, .tar.gz
#   - Linux:   .deb (Debian package with ALSA/JACK deps), AppImage, .tar.gz

set(CPACK_PACKAGE_NAME "ZYRON")
set(CPACK_PACKAGE_VENDOR "ZYRON Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native cross-platform DJ application with stems and local AI")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/zyron-dj/zyron")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "ZYRON")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
endif()

# Installation rules for the ZYRON binary target and assets
if(TARGET ZYRON)
  install(TARGETS ZYRON
    RUNTIME DESTINATION bin COMPONENT Application
    BUNDLE  DESTINATION .   COMPONENT Application)
  
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION .
    COMPONENT Documentation)
    
  install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/models"
    DESTINATION .
    COMPONENT Models
    OPTIONAL)
endif()

# Platform-specific packaging generators
if(WIN32)
  set(CPACK_GENERATOR "NSIS;WIX;ZIP")
  set(CPACK_NSIS_PACKAGE_NAME "ZYRON")
  set(CPACK_NSIS_DISPLAY_NAME "ZYRON DJ")
  set(CPACK_NSIS_HELP_LINK "https://github.com/zyron-dj/zyron")
  set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/zyron-dj/zyron")
  set(CPACK_NSIS_CONTACT "dev@zyron.audio")
  set(CPACK_NSIS_MODIFY_PATH OFF)
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)

  # WiX MSI Configuration
  set(CPACK_WIX_PRODUCT_GUID "4B5B4920-7F18-4E38-9B94-7A9B424B1001")
  set(CPACK_WIX_UPGRADE_GUID "4B5B4920-7F18-4E38-9B94-7A9B424B1002")
  set(CPACK_WIX_PROGRAM_MENU_FOLDER "ZYRON")
elseif(APPLE)
  set(CPACK_GENERATOR "DragNDrop;TGZ")
  set(CPACK_DMG_VOLUME_NAME "ZYRON")
  set(CPACK_DMG_FORMAT "UDZO")
else()
  # Linux packaging
  set(CPACK_GENERATOR "DEB;TGZ")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "ZYRON Team <dev@zyron.audio>")
  set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
  set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/zyron-dj/zyron")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libasound2, libjack-jackd2-0 | libjack0")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

include(CPack)

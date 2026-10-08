# SPDX-License-Identifier: AGPL-3.0-only
#
# The composition root: wires the modules together and owns their lifetimes. Nothing links against it.
juce_add_gui_app(ZYRON
  PRODUCT_NAME "ZYRON"
  COMPANY_NAME "ZYRON"
  VERSION "${PROJECT_VERSION}")

target_sources(ZYRON PRIVATE Main.cpp)

target_compile_definitions(ZYRON PRIVATE
  JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ZYRON,JUCE_PRODUCT_NAME>"
  JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ZYRON,JUCE_VERSION>")

target_link_libraries(ZYRON
  PRIVATE zyron::core zyron::platform zyron::platform_juce zyron::ai zyron::audio zyron::midi zyron::ui
          zyron::juce_config juce::juce_recommended_config_flags
  PUBLIC  juce::juce_recommended_warning_flags)

zyron_target_defaults(ZYRON)

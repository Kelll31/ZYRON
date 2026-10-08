# SPDX-License-Identifier: AGPL-3.0-only
#
# Analysis: BPM, beatgrid, key, energy, and waveform generation (SPEC section 31).

add_library(zyron_analysis STATIC
  Features/Fft.cpp
  Features/Stft.cpp
  Features/MelFilterbank.cpp
  Features/Resampler.cpp
  Features/Cqt.cpp
  Waveform/WaveformPeaks.cpp
  Bpm/BeatDetector.cpp
  Bpm/Beatgrid.cpp
  Bpm/BeatgridEditor.cpp
  Key/MusicalKey.cpp
  Key/KeyDetector.cpp
  Energy/EnergyAnalyzer.cpp)
add_library(zyron::analysis ALIAS zyron_analysis)

target_include_directories(zyron_analysis PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(zyron_analysis PUBLIC zyron::core)
zyron_target_defaults(zyron_analysis)

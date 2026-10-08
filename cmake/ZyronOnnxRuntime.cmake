# SPDX-License-Identifier: AGPL-3.0-only
#
# ONNX Runtime (MIT) with the DirectML execution provider, from the official Microsoft packages on nuget.org
# (ADR-0014). One onnxruntime.dll serves CPU and any DirectX 12 GPU (NVIDIA, AMD, Intel) with no CUDA/cuDNN install.
# Windows only for now; on other platforms the ONNX backend is simply not built (the AI features that need it report
# "runtime unavailable").
include_guard(GLOBAL)

option(ZYRON_ENABLE_ONNX "Build the ONNX Runtime backend (downloads onnxruntime from nuget.org)" ON)

set(ZYRON_ORT_VERSION "1.24.4")
set(ZYRON_ORT_SHA256 "57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681")
set(ZYRON_DML_VERSION "1.15.4")
set(ZYRON_DML_SHA256 "4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9")

# Downloads `url` to `file` unless a file with the expected hash is already there.
function(_zyron_ort_download url file sha256)
  if(EXISTS "${file}")
    file(SHA256 "${file}" _have)
    if(_have STREQUAL sha256)
      return()
    endif()
  endif()
  message(STATUS "Downloading ${url}")
  file(DOWNLOAD "${url}" "${file}" EXPECTED_HASH SHA256=${sha256} SHOW_PROGRESS STATUS _status)
  list(GET _status 0 _code)
  if(NOT _code EQUAL 0)
    file(REMOVE "${file}")
    message(FATAL_ERROR "Download failed: ${url} (${_status}). Configure with -DZYRON_ENABLE_ONNX=OFF to build without it.")
  endif()
endfunction()

macro(zyron_require_onnxruntime)
  if(ZYRON_ENABLE_ONNX AND WIN32 AND NOT TARGET zyron_onnxruntime)
    set(_ort_dir "${CMAKE_BINARY_DIR}/_ort")
    file(MAKE_DIRECTORY "${_ort_dir}")
    set(_ort_pkg "${_ort_dir}/onnxruntime-directml-${ZYRON_ORT_VERSION}.nupkg")
    set(_dml_pkg "${_ort_dir}/directml-${ZYRON_DML_VERSION}.nupkg")
    _zyron_ort_download(
      "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/${ZYRON_ORT_VERSION}/microsoft.ml.onnxruntime.directml.${ZYRON_ORT_VERSION}.nupkg"
      "${_ort_pkg}" "${ZYRON_ORT_SHA256}")
    _zyron_ort_download(
      "https://api.nuget.org/v3-flatcontainer/microsoft.ai.directml/${ZYRON_DML_VERSION}/microsoft.ai.directml.${ZYRON_DML_VERSION}.nupkg"
      "${_dml_pkg}" "${ZYRON_DML_SHA256}")

    if(NOT EXISTS "${_ort_dir}/ort/runtimes/win-x64/native/onnxruntime.dll")
      file(ARCHIVE_EXTRACT INPUT "${_ort_pkg}" DESTINATION "${_ort_dir}/ort"
           PATTERNS "runtimes/win-x64/native/*" "build/native/include/*" "LICENSE" "ThirdPartyNotices.txt")
    endif()
    if(NOT EXISTS "${_ort_dir}/dml/bin/x64-win/DirectML.dll")
      file(ARCHIVE_EXTRACT INPUT "${_dml_pkg}" DESTINATION "${_ort_dir}/dml"
           PATTERNS "bin/x64-win/DirectML.dll" "LICENSE.txt")
    endif()

    add_library(zyron_onnxruntime SHARED IMPORTED GLOBAL)
    set_target_properties(zyron_onnxruntime PROPERTIES
      IMPORTED_LOCATION "${_ort_dir}/ort/runtimes/win-x64/native/onnxruntime.dll"
      IMPORTED_IMPLIB "${_ort_dir}/ort/runtimes/win-x64/native/onnxruntime.lib"
      INTERFACE_INCLUDE_DIRECTORIES "${_ort_dir}/ort/build/native/include")
    add_library(zyron::onnxruntime ALIAS zyron_onnxruntime)

    # Files that must sit next to the executable. onnxruntime.dll is found by TARGET_RUNTIME_DLLS; the provider DLL and
    # DirectML are loaded at run time, so they are listed here and copied by zyron_copy_onnx_runtime().
    set_property(GLOBAL PROPERTY ZYRON_ORT_RUNTIME_FILES
      "${_ort_dir}/ort/runtimes/win-x64/native/onnxruntime.dll"
      "${_ort_dir}/ort/runtimes/win-x64/native/onnxruntime_providers_shared.dll"
      "${_ort_dir}/dml/bin/x64-win/DirectML.dll")
  endif()
endmacro()

# Copies the ONNX Runtime DLLs next to `target` after it is built (no-op when the backend is off).
function(zyron_copy_onnx_runtime target)
  if(TARGET zyron_onnxruntime)
    get_property(_files GLOBAL PROPERTY ZYRON_ORT_RUNTIME_FILES)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_files} "$<TARGET_FILE_DIR:${target}>"
      COMMAND_EXPAND_LISTS)
  endif()
endfunction()

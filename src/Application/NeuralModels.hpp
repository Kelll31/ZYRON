// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "AI/Runtime/AIRuntime.hpp"

#ifdef ZYRON_HAS_ONNX
#include "AI/Backends/Cpu/CpuBackend.hpp"
#include "AI/Backends/Onnx/OnnxRuntime.hpp"
#endif

namespace zyron::application {

/// Finds the downloaded neural networks and hands out one shared inference session per model (SPEC sections 73, 74).
///
/// Weights are never bundled: they live in a models folder filled by scripts/download_models.ps1 (later by the Model
/// Manager). The folder is looked up in this order: the ZYRON_MODELS_DIR environment variable, <app data>/models, a
/// models folder next to the executable, then every parent folder of the executable (a development checkout).
///
/// Sessions are created on first use, on the CPU: ONNX Runtime on the CPU runs HTDemucs about 6x faster than real time
/// and Beat This! in a few seconds per track, and the DirectML provider turned out to be much slower than that on the
/// Demucs graph (ADR-0014). They are safe to share between threads.
class NeuralModels {
 public:
  explicit NeuralModels(const std::filesystem::path& appDataDir);

  /// The folder holding the weights, or empty if none was found.
  [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }
  /// True when the build contains the ONNX runtime and a models folder was found.
  [[nodiscard]] bool available() const noexcept;

  /// Shared session for a model, created on first use; null with `error` filled when it cannot be had.
  [[nodiscard]] std::shared_ptr<ai::IModelSession> demucs(std::string* error);
  [[nodiscard]] std::shared_ptr<ai::IModelSession> beatThis(std::string* error);
  [[nodiscard]] std::shared_ptr<ai::IModelSession> skey(std::string* error);
  /// The mel filterbank that belongs to Beat This! (513 x 128 floats, row-major); empty with `error` filled on failure.
  [[nodiscard]] std::vector<float> beatThisMelFilterbank(std::string* error) const;

 private:
  [[nodiscard]] std::shared_ptr<ai::IModelSession> session(std::shared_ptr<ai::IModelSession>& slot,
                                                           const char* relativePath, std::string* error);

  std::filesystem::path directory_;
  std::mutex mutex_;
  std::shared_ptr<ai::IModelSession> demucs_;
  std::shared_ptr<ai::IModelSession> beatThis_;
  std::shared_ptr<ai::IModelSession> skey_;
#ifdef ZYRON_HAS_ONNX
  ai::OnnxRuntime runtime_;
  ai::CpuBackend cpu_;
#endif
};

}  // namespace zyron::application

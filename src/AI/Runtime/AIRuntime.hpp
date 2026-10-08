// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "AI/Backends/GPUBackend.hpp"
#include "AI/Runtime/Tensor.hpp"

namespace zyron::ai {

/// Supported deep learning model inference execution runtimes (SPEC section 34).
enum class RuntimeType : std::uint8_t {
  OnnxRuntime = 0,
  LibTorch,
  CpuFallback,
  Sidecar
};

[[nodiscard]] constexpr std::string_view runtimeTypeName(RuntimeType type) noexcept {
  switch (type) {
    case RuntimeType::OnnxRuntime:
      return "ONNX Runtime";
    case RuntimeType::LibTorch:
      return "LibTorch";
    case RuntimeType::CpuFallback:
      return "CPU Fallback";
    case RuntimeType::Sidecar:
      return "Sidecar";
  }
  return "Unknown";
}

/// Active model execution session hosting an instantiated neural network graph (SPEC section 34).
class IModelSession {
 public:
  virtual ~IModelSession() = default;

  [[nodiscard]] virtual std::vector<std::string> inputNames() const = 0;
  [[nodiscard]] virtual std::vector<std::string> outputNames() const = 0;

  /// Executes inference synchronously.
  /// \param inputs Vector of input tensors corresponding to inputNames().
  /// \return Vector of output tensors corresponding to outputNames().
  virtual std::vector<Tensor> run(const std::vector<Tensor>& inputs) = 0;
};

/// Abstract AI inference engine runtime provider (SPEC sections 33, 34, 79).
/// Insulates the application, stem separation, and analysis layers from the concrete engine (ONNX Runtime, LibTorch, etc.).
class AIRuntime {
 public:
  virtual ~AIRuntime() = default;

  [[nodiscard]] virtual RuntimeType type() const noexcept = 0;
  [[nodiscard]] virtual std::string name() const = 0;
  [[nodiscard]] virtual bool isAvailable() const noexcept = 0;

  /// Loads an AI model from disk into an execution session with the specified compute backend.
  virtual std::unique_ptr<IModelSession> createSession(const std::string& modelPath,
                                                       const GPUBackend& backend,
                                                       int deviceIndex = 0) = 0;

  /// Loads an AI model from in-memory byte buffer into an execution session.
  virtual std::unique_ptr<IModelSession> createSessionFromMemory(const void* data,
                                                                 std::size_t sizeBytes,
                                                                 const GPUBackend& backend,
                                                                 int deviceIndex = 0) = 0;
};

}  // namespace zyron::ai

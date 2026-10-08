// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/Backends/Onnx/OnnxRuntime.hpp"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>

#include <dml_provider_factory.h>

namespace zyron::ai {

namespace {

/// One process-wide environment: ONNX Runtime allows exactly one, and sessions keep it alive.
Ort::Env& environment() {
  static Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "zyron");  // the "unused initializer" warnings of the exports are noise
  return env;
}

class OnnxSession final : public IModelSession {
 public:
  explicit OnnxSession(Ort::Session session) : session_(std::move(session)) {
    Ort::AllocatorWithDefaultOptions allocator;
    for (std::size_t i = 0; i < session_.GetInputCount(); ++i) {
      inputNames_.emplace_back(session_.GetInputNameAllocated(i, allocator).get());
    }
    for (std::size_t i = 0; i < session_.GetOutputCount(); ++i) {
      outputNames_.emplace_back(session_.GetOutputNameAllocated(i, allocator).get());
    }
  }

  [[nodiscard]] std::vector<std::string> inputNames() const override { return inputNames_; }
  [[nodiscard]] std::vector<std::string> outputNames() const override { return outputNames_; }

  std::vector<Tensor> run(const std::vector<Tensor>& inputs) override {
    if (inputs.size() != inputNames_.size()) {
      throw std::invalid_argument("model expects " + std::to_string(inputNames_.size()) + " inputs, got " +
                                  std::to_string(inputs.size()));
    }
    const Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    std::vector<Ort::Value> values;
    values.reserve(inputs.size());
    for (const Tensor& input : inputs) {
      if (input.type() != DataType::Float32) {
        throw std::invalid_argument("only float32 tensors are supported");
      }
      const std::vector<std::int64_t>& dims = input.shape().dims;
      // ONNX Runtime never writes to an input tensor; the const_cast only satisfies the C API signature.
      values.push_back(Ort::Value::CreateTensor<float>(memory, const_cast<float*>(input.data()), input.size(),
                                                       dims.data(), dims.size()));
    }

    std::vector<const char*> inputNames;
    for (const auto& name : inputNames_) {
      inputNames.push_back(name.c_str());
    }
    std::vector<const char*> outputNames;
    for (const auto& name : outputNames_) {
      outputNames.push_back(name.c_str());
    }

    std::vector<Ort::Value> outputs = session_.Run(Ort::RunOptions{nullptr}, inputNames.data(), values.data(),
                                                   values.size(), outputNames.data(), outputNames.size());

    std::vector<Tensor> result;
    result.reserve(outputs.size());
    for (Ort::Value& output : outputs) {
      const auto info = output.GetTensorTypeAndShapeInfo();
      if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        throw std::runtime_error("model returned a tensor that is not float32");
      }
      const std::size_t count = info.GetElementCount();
      const float* data = output.GetTensorData<float>();
      result.emplace_back(TensorShape(info.GetShape()), std::vector<float>(data, data + count));
    }
    return result;
  }

 private:
  Ort::Session session_;
  std::vector<std::string> inputNames_;
  std::vector<std::string> outputNames_;
};

Ort::SessionOptions makeOptions(const GPUBackend& backend, int deviceIndex) {
  Ort::SessionOptions options;
  options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
  if (backend.type() == BackendType::DirectML) {
    // The DirectML provider requires these two settings.
    options.DisableMemPattern();
    options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(options, deviceIndex));
  }
  return options;
}

}  // namespace

bool DirectMlBackend::isAvailable() const noexcept {
  return true;  // the provider ships with the runtime; a missing adapter shows up when a session is created
}

std::string DirectMlBackend::version() const {
  return "ONNX Runtime " + std::string(OrtGetApiBase()->GetVersionString());
}

OnnxRuntime::OnnxRuntime() = default;
OnnxRuntime::~OnnxRuntime() = default;

std::string OnnxRuntime::name() const {
  return "ONNX Runtime " + std::string(OrtGetApiBase()->GetVersionString());
}

std::unique_ptr<IModelSession> OnnxRuntime::createSession(const std::string& modelPath, const GPUBackend& backend,
                                                          int deviceIndex) {
  lastError_.clear();
  try {
    const Ort::SessionOptions options = makeOptions(backend, deviceIndex);
    const std::u8string utf8(reinterpret_cast<const char8_t*>(modelPath.data()), modelPath.size());
    const std::filesystem::path path(utf8);  // ONNX Runtime on Windows takes a wide path
    return std::make_unique<OnnxSession>(Ort::Session(environment(), path.c_str(), options));
  } catch (const std::exception& error) {
    lastError_ = error.what();
    return nullptr;
  }
}

std::unique_ptr<IModelSession> OnnxRuntime::createSessionFromMemory(const void* data, std::size_t sizeBytes,
                                                                    const GPUBackend& backend, int deviceIndex) {
  lastError_.clear();
  try {
    const Ort::SessionOptions options = makeOptions(backend, deviceIndex);
    return std::make_unique<OnnxSession>(Ort::Session(environment(), data, sizeBytes, options));
  } catch (const std::exception& error) {
    lastError_ = error.what();
    return nullptr;
  }
}

}  // namespace zyron::ai

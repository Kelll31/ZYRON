// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace zyron::ai {

enum class DataType : std::uint8_t {
  Float32 = 0,
  Float16,
  Int64,
  Int32
};

struct TensorShape {
  std::vector<std::int64_t> dims;

  TensorShape() = default;
  explicit TensorShape(std::vector<std::int64_t> d) : dims(std::move(d)) {}

  [[nodiscard]] std::size_t rank() const noexcept { return dims.size(); }

  [[nodiscard]] std::int64_t dim(std::size_t axis) const {
    if (axis >= dims.size()) {
      throw std::out_of_range("TensorShape axis out of range");
    }
    return dims[axis];
  }

  [[nodiscard]] std::int64_t totalElements() const noexcept {
    if (dims.empty()) return 0;
    std::int64_t count = 1;
    for (const auto d : dims) {
      if (d <= 0) return 0;
      count *= d;
    }
    return count;
  }

  friend bool operator==(const TensorShape&, const TensorShape&) = default;
};

/// High-performance multi-dimensional tensor buffer for model inputs and outputs (SPEC section 34).
class Tensor {
 public:
  Tensor() = default;

  explicit Tensor(TensorShape shape, DataType type = DataType::Float32)
      : shape_(std::move(shape)), type_(type) {
    data_.resize(static_cast<std::size_t>(shape_.totalElements()), 0.0f);
  }

  Tensor(TensorShape shape, std::vector<float> data, DataType type = DataType::Float32)
      : shape_(std::move(shape)), type_(type), data_(std::move(data)) {
    if (data_.size() != static_cast<std::size_t>(shape_.totalElements())) {
      throw std::invalid_argument("Tensor data size does not match shape dimensions");
    }
  }

  [[nodiscard]] const TensorShape& shape() const noexcept { return shape_; }
  [[nodiscard]] DataType type() const noexcept { return type_; }

  [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
  [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

  [[nodiscard]] float* data() noexcept { return data_.data(); }
  [[nodiscard]] const float* data() const noexcept { return data_.data(); }

  [[nodiscard]] std::vector<float>& buffer() noexcept { return data_; }
  [[nodiscard]] const std::vector<float>& buffer() const noexcept { return data_; }

  [[nodiscard]] float& operator[](std::size_t index) noexcept { return data_[index]; }
  [[nodiscard]] const float& operator[](std::size_t index) const noexcept { return data_[index]; }

  [[nodiscard]] float at(std::size_t index) const {
    return data_.at(index);
  }

  void reshape(TensorShape newShape) {
    if (newShape.totalElements() != shape_.totalElements()) {
      throw std::invalid_argument("Cannot reshape tensor: element count mismatch");
    }
    shape_ = std::move(newShape);
  }

 private:
  TensorShape shape_;
  DataType type_{DataType::Float32};
  std::vector<float> data_;
};

}  // namespace zyron::ai

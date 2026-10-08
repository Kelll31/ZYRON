// SPDX-License-Identifier: AGPL-3.0-only
#include "support/AllocationGuard.hpp"

#include <cstdlib>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace zyron::test::detail {

namespace {

thread_local int t_guardDepth = 0;
thread_local int t_bypassDepth = 0;
thread_local RealtimeAllocationReport t_report{};

inline void* allocateAligned(std::size_t size, std::size_t alignment) noexcept {
#if defined(_WIN32)
  return _aligned_malloc(size, alignment);
#else
  void* ptr = nullptr;
  const std::size_t adjAlignment = alignment < sizeof(void*) ? sizeof(void*) : alignment;
  if (posix_memalign(&ptr, adjAlignment, size) != 0) {
    return nullptr;
  }
  return ptr;
#endif
}

inline void deallocateAligned(void* ptr) noexcept {
#if defined(_WIN32)
  _aligned_free(ptr);
#else
  std::free(ptr);
#endif
}

}  // namespace

void enterRealtimeGuard() noexcept {
  ++t_guardDepth;
}

void exitRealtimeGuard() noexcept {
  if (t_guardDepth > 0) {
    --t_guardDepth;
  }
}

RealtimeAllocationReport currentRealtimeReport() noexcept {
  return t_report;
}

void resetRealtimeReport() noexcept {
  t_report = RealtimeAllocationReport{};
}

void enterGuardBypass() noexcept {
  ++t_bypassDepth;
}

void exitGuardBypass() noexcept {
  if (t_bypassDepth > 0) {
    --t_bypassDepth;
  }
}

inline void recordAlloc(std::size_t size) noexcept {
  if (t_guardDepth > 0 && t_bypassDepth == 0) {
    ++t_report.allocationCount;
    t_report.allocatedBytes += size;
  }
}

inline void recordDealloc() noexcept {
  if (t_guardDepth > 0 && t_bypassDepth == 0) {
    ++t_report.deallocationCount;
  }
}

}  // namespace zyron::test::detail

// --- Global replaceable allocation functions (C++ standard) ---

void* operator new(std::size_t size) {
  zyron::test::detail::recordAlloc(size);
  void* const ptr = std::malloc(size == 0 ? 1 : size);
  if (ptr == nullptr) {
    throw std::bad_alloc();
  }
  return ptr;
}

void* operator new[](std::size_t size) {
  zyron::test::detail::recordAlloc(size);
  void* const ptr = std::malloc(size == 0 ? 1 : size);
  if (ptr == nullptr) {
    throw std::bad_alloc();
  }
  return ptr;
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  zyron::test::detail::recordAlloc(size);
  return std::malloc(size == 0 ? 1 : size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  zyron::test::detail::recordAlloc(size);
  return std::malloc(size == 0 ? 1 : size);
}

void operator delete(void* ptr) noexcept {
  if (ptr != nullptr) {
    zyron::test::detail::recordDealloc();
    std::free(ptr);
  }
}

void operator delete[](void* ptr) noexcept {
  if (ptr != nullptr) {
    zyron::test::detail::recordDealloc();
    std::free(ptr);
  }
}

void operator delete(void* ptr, std::size_t /*size*/) noexcept {
  if (ptr != nullptr) {
    zyron::test::detail::recordDealloc();
    std::free(ptr);
  }
}

void operator delete[](void* ptr, std::size_t /*size*/) noexcept {
  if (ptr != nullptr) {
    zyron::test::detail::recordDealloc();
    std::free(ptr);
  }
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
  if (ptr != nullptr) {
    zyron::test::detail::recordDealloc();
    std::free(ptr);
  }
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
  if (ptr != nullptr) {
    zyron::test::detail::recordDealloc();
    std::free(ptr);
  }
}

#if __cpp_aligned_new
void* operator new(std::size_t size, std::align_val_t al) {
  zyron::test::detail::recordAlloc(size);
  void* const ptr = zyron::test::detail::allocateAligned(size == 0 ? 1 : size, static_cast<std::size_t>(al));
  if (ptr == nullptr) {
    throw std::bad_alloc();
  }
  return ptr;
}

void* operator new[](std::size_t size, std::align_val_t al) {
  zyron::test::detail::recordAlloc(size);
  void* const ptr = zyron::test::detail::allocateAligned(size == 0 ? 1 : size, static_cast<std::size_t>(al));
  if (ptr == nullptr) {
    throw std::bad_alloc();
  }
  return ptr;
}

void* operator new(std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
  zyron::test::detail::recordAlloc(size);
  return zyron::test::detail::allocateAligned(size == 0 ? 1 : size, static_cast<std::size_t>(al));
}

void* operator new[](std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
  zyron::test::detail::recordAlloc(size);
  return zyron::test::detail::allocateAligned(size == 0 ? 1 : size, static_cast<std::size_t>(al));
}

void operator delete(void* ptr, std::align_val_t) noexcept {
  if (ptr != nullptr) {
    zyron::test::detail::recordDealloc();
    zyron::test::detail::deallocateAligned(ptr);
  }
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
  if (ptr != nullptr) {
    zyron::test::detail::recordDealloc();
    zyron::test::detail::deallocateAligned(ptr);
  }
}

void operator delete(void* ptr, std::size_t /*size*/, std::align_val_t) noexcept {
  if (ptr != nullptr) {
    zyron::test::detail::recordDealloc();
    zyron::test::detail::deallocateAligned(ptr);
  }
}

void operator delete[](void* ptr, std::size_t /*size*/, std::align_val_t) noexcept {
  if (ptr != nullptr) {
    zyron::test::detail::recordDealloc();
    zyron::test::detail::deallocateAligned(ptr);
  }
}

void operator delete(void* ptr, std::align_val_t, const std::nothrow_t&) noexcept {
  if (ptr != nullptr) {
    zyron::test::detail::recordDealloc();
    zyron::test::detail::deallocateAligned(ptr);
  }
}

void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t&) noexcept {
  if (ptr != nullptr) {
    zyron::test::detail::recordDealloc();
    zyron::test::detail::deallocateAligned(ptr);
  }
}
#endif

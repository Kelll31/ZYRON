// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

namespace zyron::test {

/// Statistics about memory allocations and deallocations while in a real-time guard.
struct RealtimeAllocationReport {
  std::size_t allocationCount{0};
  std::size_t allocatedBytes{0};
  std::size_t deallocationCount{0};

  [[nodiscard]] bool hasViolations() const noexcept { return allocationCount > 0 || deallocationCount > 0; }
};

namespace detail {
void enterRealtimeGuard() noexcept;
void exitRealtimeGuard() noexcept;
RealtimeAllocationReport currentRealtimeReport() noexcept;
void resetRealtimeReport() noexcept;
void enterGuardBypass() noexcept;
void exitGuardBypass() noexcept;
}  // namespace detail

/// RAII scope that forbids any heap allocation (new/malloc) or deallocation (delete/free)
/// on the current thread. While in scope, any heap activity increments the violation counters.
///
/// Intended for testing real-time audio code (ROADMAP P1-10, ARCHITECTURE section 4).
class ScopedRealtimeGuard {
 public:
  ScopedRealtimeGuard() noexcept {
    detail::resetRealtimeReport();
    detail::enterRealtimeGuard();
  }

  ~ScopedRealtimeGuard() noexcept { detail::exitRealtimeGuard(); }

  ScopedRealtimeGuard(const ScopedRealtimeGuard&) = delete;
  ScopedRealtimeGuard& operator=(const ScopedRealtimeGuard&) = delete;
  ScopedRealtimeGuard(ScopedRealtimeGuard&&) = delete;
  ScopedRealtimeGuard& operator=(ScopedRealtimeGuard&&) = delete;

  [[nodiscard]] RealtimeAllocationReport report() const noexcept { return detail::currentRealtimeReport(); }

  [[nodiscard]] std::size_t allocationCount() const noexcept { return detail::currentRealtimeReport().allocationCount; }

  [[nodiscard]] std::size_t allocatedBytes() const noexcept { return detail::currentRealtimeReport().allocatedBytes; }

  [[nodiscard]] std::size_t deallocationCount() const noexcept {
    return detail::currentRealtimeReport().deallocationCount;
  }

  [[nodiscard]] bool hasViolations() const noexcept { return detail::currentRealtimeReport().hasViolations(); }
};

/// Temporarily bypasses the guard on the current thread (e.g. to format an error message or
/// perform fixture setup while maintaining the outer guard structure).
class ScopedGuardBypass {
 public:
  ScopedGuardBypass() noexcept { detail::enterGuardBypass(); }

  ~ScopedGuardBypass() noexcept { detail::exitGuardBypass(); }

  ScopedGuardBypass(const ScopedGuardBypass&) = delete;
  ScopedGuardBypass& operator=(const ScopedGuardBypass&) = delete;
  ScopedGuardBypass(ScopedGuardBypass&&) = delete;
  ScopedGuardBypass& operator=(ScopedGuardBypass&&) = delete;
};

}  // namespace zyron::test

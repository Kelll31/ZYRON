// SPDX-License-Identifier: AGPL-3.0-only
#include "Core/State/AppState.hpp"

#include <utility>

namespace zyron::core {

StateStore::StateStore() : current_(std::make_shared<const AppState>()) {}

std::shared_ptr<const AppState> StateStore::snapshot() const {
  const std::scoped_lock lock(mutex_);
  return current_;
}

bool StateStore::publishIfRevision(std::uint64_t expectedRevision, AppState next) {
  auto fresh = std::make_shared<const AppState>(std::move(next));  // allocate outside the lock
  std::shared_ptr<const AppState> retired;                         // destroyed after the lock is released
  {
    const std::scoped_lock lock(mutex_);
    if (current_->revision != expectedRevision) {
      return false;
    }
    retired = std::move(current_);
    current_ = std::move(fresh);
  }
  return true;
}

}  // namespace zyron::core

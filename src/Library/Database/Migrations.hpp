// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Library/Database/Database.hpp"

namespace zyron::library {

/// Applies all pending schema migrations sequentially using PRAGMA user_version (ARCHITECTURE section 9).
/// Each migration executes within an explicit transaction.
class Migrations {
 public:
  static constexpr int kCurrentSchemaVersion = 3;

  /// Applies migrations until current version is reached.
  static void apply(Database& db);
};

}  // namespace zyron::library

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Library/LibraryTypes.hpp"
#include "Library/Database/Database.hpp"
#include "Library/Scanner/LibraryScanner.hpp"

namespace zyron::library {

/// SQLite and LibraryScanner backed implementation of core::ILibrarySource (SPEC sections 28, 30).
class SqliteLibrarySource : public core::ILibrarySource {
 public:
  explicit SqliteLibrarySource(std::shared_ptr<Database> db,
                               std::shared_ptr<LibraryScanner> scanner = nullptr);
  ~SqliteLibrarySource() override = default;

  [[nodiscard]] std::vector<core::TrackItem> search(std::string_view query) override;
  [[nodiscard]] std::vector<core::TrackItem> listAll() override;
  void requestScan(const std::string& folderPath) override;

 private:
  std::shared_ptr<Database> db_;
  std::shared_ptr<LibraryScanner> scanner_;
};

}  // namespace zyron::library

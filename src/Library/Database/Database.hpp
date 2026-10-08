// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace zyron::library {

class Statement;

/// RAII wrapper around SQLite database connection (SPEC section 28, ARCHITECTURE section 9).
/// Configured with WAL mode, foreign keys, and normal synchronous durability.
class Database {
 public:
  Database();
  ~Database();

  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
  Database(Database&& other) noexcept;
  Database& operator=(Database&& other) noexcept;

  /// Opens or creates database file at path. Sets WAL mode and PRAGMA settings.
  static Database open(const std::filesystem::path& path);

  /// Opens an in-memory database (useful for isolated tests).
  static Database openInMemory();

  /// Executes raw SQL query (e.g. DDL or multi-statement pragmas). Throws on failure.
  void execute(std::string_view sql);

  /// Prepares a statement for execution. Throws on compilation error.
  [[nodiscard]] Statement prepare(std::string_view sql);

  /// Transaction controls
  void beginTransaction();
  void commit();
  void rollback();

  [[nodiscard]] std::int64_t lastInsertRowId() const noexcept;
  [[nodiscard]] int changes() const noexcept;
  [[nodiscard]] int userVersion() const;
  void setUserVersion(int version);

  [[nodiscard]] bool isOpen() const noexcept { return db_ != nullptr; }
  [[nodiscard]] sqlite3* handle() const noexcept { return db_; }

 private:
  explicit Database(sqlite3* db);
  void configurePragmas();

  sqlite3* db_{nullptr};
};

/// RAII wrapper around compiled SQLite prepared statement.
class Statement {
 public:
  Statement(sqlite3* db, std::string_view sql);
  ~Statement();

  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;
  Statement(Statement&& other) noexcept;
  Statement& operator=(Statement&& other) noexcept;

  // Bind values (1-based index)
  void bindInt(int index, int value);
  void bindInt64(int index, std::int64_t value);
  void bindDouble(int index, double value);
  void bindText(int index, std::string_view value);
  void bindNull(int index);

  /// Steps statement execution. Returns true if row available, false if completed.
  bool step();

  /// Executes statement without expecting return rows.
  void execute();

  // Column access (0-based index)
  [[nodiscard]] int getInt(int column) const;
  [[nodiscard]] std::int64_t getInt64(int column) const;
  [[nodiscard]] double getDouble(int column) const;
  [[nodiscard]] std::string getText(int column) const;
  [[nodiscard]] bool isNull(int column) const;

  void reset() noexcept;
  void clearBindings() noexcept;

 private:
  sqlite3* db_{nullptr};
  sqlite3_stmt* stmt_{nullptr};
};

}  // namespace zyron::library

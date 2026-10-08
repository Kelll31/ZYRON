// SPDX-License-Identifier: AGPL-3.0-only
#include "Library/Database/Database.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <string>

namespace zyron::library {

Database::Database() = default;

Database::Database(sqlite3* db) : db_(db) {}

Database::~Database() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

Database::Database(Database&& other) noexcept : db_(other.db_) {
  other.db_ = nullptr;
}

Database& Database::operator=(Database&& other) noexcept {
  if (this != &other) {
    if (db_ != nullptr) {
      sqlite3_close(db_);
    }
    db_ = other.db_;
    other.db_ = nullptr;
  }
  return *this;
}

void Database::configurePragmas() {
  execute("PRAGMA journal_mode = WAL;");
  execute("PRAGMA synchronous = NORMAL;");
  execute("PRAGMA foreign_keys = ON;");
  execute("PRAGMA busy_timeout = 5000;");
}

Database Database::open(const std::filesystem::path& path) {
  sqlite3* db = nullptr;
  const auto u8 = path.u8string();
  const auto* utf8Path = reinterpret_cast<const char*>(u8.c_str());
  const int rc = sqlite3_open(utf8Path, &db);
  if (rc != SQLITE_OK) {
    const std::string msg = db ? sqlite3_errmsg(db) : "Failed to allocate sqlite3 handle";
    if (db) {
      sqlite3_close(db);
    }
    throw std::runtime_error("Failed to open database at " + path.string() + ": " + msg);
  }

  Database database(db);
  database.configurePragmas();
  return database;
}

Database Database::openInMemory() {
  sqlite3* db = nullptr;
  const int rc = sqlite3_open(":memory:", &db);
  if (rc != SQLITE_OK) {
    const std::string msg = db ? sqlite3_errmsg(db) : "Failed to open in-memory db";
    if (db) {
      sqlite3_close(db);
    }
    throw std::runtime_error("Failed to open in-memory database: " + msg);
  }

  Database database(db);
  database.execute("PRAGMA foreign_keys = ON;");
  return database;
}

void Database::execute(std::string_view sql) {
  if (db_ == nullptr) {
    throw std::runtime_error("Database is not open");
  }

  char* errMsg = nullptr;
  const int rc = sqlite3_exec(db_, sql.data(), nullptr, nullptr, &errMsg);
  if (rc != SQLITE_OK) {
    std::string err = errMsg ? errMsg : sqlite3_errmsg(db_);
    sqlite3_free(errMsg);
    throw std::runtime_error("SQLite execute error (" + std::to_string(rc) + "): " + err);
  }
}

Statement Database::prepare(std::string_view sql) {
  if (db_ == nullptr) {
    throw std::runtime_error("Database is not open");
  }
  return Statement(db_, sql);
}

void Database::beginTransaction() {
  execute("BEGIN IMMEDIATE TRANSACTION;");
}

void Database::commit() {
  execute("COMMIT;");
}

void Database::rollback() {
  execute("ROLLBACK;");
}

std::int64_t Database::lastInsertRowId() const noexcept {
  return db_ ? sqlite3_last_insert_rowid(db_) : 0;
}

int Database::changes() const noexcept {
  return db_ ? sqlite3_changes(db_) : 0;
}

int Database::userVersion() const {
  if (db_ == nullptr) {
    return 0;
  }
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "PRAGMA user_version;", -1, &stmt, nullptr) != SQLITE_OK) {
    return 0;
  }
  int ver = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    ver = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return ver;
}

void Database::setUserVersion(int version) {
  execute("PRAGMA user_version = " + std::to_string(version) + ";");
}

// ---------------------------------------------------------------------------
// Statement Implementation
// ---------------------------------------------------------------------------

Statement::Statement(sqlite3* db, std::string_view sql) : db_(db) {
  const int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr);
  if (rc != SQLITE_OK) {
    throw std::runtime_error("SQLite prepare error (" + std::to_string(rc) + "): " + sqlite3_errmsg(db_));
  }
}

Statement::~Statement() {
  if (stmt_ != nullptr) {
    sqlite3_finalize(stmt_);
    stmt_ = nullptr;
  }
}

Statement::Statement(Statement&& other) noexcept : db_(other.db_), stmt_(other.stmt_) {
  other.db_ = nullptr;
  other.stmt_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
  if (this != &other) {
    if (stmt_ != nullptr) {
      sqlite3_finalize(stmt_);
    }
    db_ = other.db_;
    stmt_ = other.stmt_;
    other.db_ = nullptr;
    other.stmt_ = nullptr;
  }
  return *this;
}

void Statement::bindInt(int index, int value) {
  const int rc = sqlite3_bind_int(stmt_, index, value);
  if (rc != SQLITE_OK) {
    throw std::runtime_error("sqlite3_bind_int failed: " + std::to_string(rc));
  }
}

void Statement::bindInt64(int index, std::int64_t value) {
  const int rc = sqlite3_bind_int64(stmt_, index, value);
  if (rc != SQLITE_OK) {
    throw std::runtime_error("sqlite3_bind_int64 failed: " + std::to_string(rc));
  }
}

void Statement::bindDouble(int index, double value) {
  const int rc = sqlite3_bind_double(stmt_, index, value);
  if (rc != SQLITE_OK) {
    throw std::runtime_error("sqlite3_bind_double failed: " + std::to_string(rc));
  }
}

void Statement::bindText(int index, std::string_view value) {
  const int rc = sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    throw std::runtime_error("sqlite3_bind_text failed: " + std::to_string(rc));
  }
}

void Statement::bindNull(int index) {
  const int rc = sqlite3_bind_null(stmt_, index);
  if (rc != SQLITE_OK) {
    throw std::runtime_error("sqlite3_bind_null failed: " + std::to_string(rc));
  }
}

bool Statement::step() {
  const int rc = sqlite3_step(stmt_);
  if (rc == SQLITE_ROW) {
    return true;
  }
  if (rc == SQLITE_DONE) {
    return false;
  }
  throw std::runtime_error("SQLite step error (" + std::to_string(rc) + "): " + sqlite3_errmsg(db_));
}

void Statement::execute() {
  while (step()) {
  }
}

int Statement::getInt(int column) const {
  return sqlite3_column_int(stmt_, column);
}

std::int64_t Statement::getInt64(int column) const {
  return sqlite3_column_int64(stmt_, column);
}

double Statement::getDouble(int column) const {
  return sqlite3_column_double(stmt_, column);
}

std::string Statement::getText(int column) const {
  const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, column));
  if (text == nullptr) {
    return {};
  }
  const int bytes = sqlite3_column_bytes(stmt_, column);
  return std::string(text, static_cast<std::size_t>(bytes));
}

bool Statement::isNull(int column) const {
  return sqlite3_column_type(stmt_, column) == SQLITE_NULL;
}

void Statement::reset() noexcept {
  if (stmt_ != nullptr) {
    sqlite3_reset(stmt_);
  }
}

void Statement::clearBindings() noexcept {
  if (stmt_ != nullptr) {
    sqlite3_clear_bindings(stmt_);
  }
}

}  // namespace zyron::library

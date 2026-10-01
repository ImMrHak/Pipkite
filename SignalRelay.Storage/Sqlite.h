#pragma once
// Thin RAII wrapper over the SQLite C API. No exceptions escape; errors are explicit.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "SignalRelay.Core/Result.h"

struct sqlite3;
struct sqlite3_stmt;

namespace sr::db {

class Statement;

class Database {
public:
    Database() = default;
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&& o) noexcept : db_(o.db_) { o.db_ = nullptr; }

    // Opens with WAL, foreign keys, busy timeout and synchronous=FULL for durable critical commits.
    Status open(const std::string& utf8Path, bool readOnly = false);
    void close();
    bool isOpen() const { return db_ != nullptr; }
    Status exec(std::string_view sql);
    Result<Statement> prepare(std::string_view sql);
    std::int64_t lastInsertRowId() const;
    int changes() const;
    std::string lastError() const;
    sqlite3* handle() const { return db_; }

private:
    sqlite3* db_ = nullptr;
};

class Statement {
public:
    Statement() = default;
    explicit Statement(sqlite3_stmt* s) : s_(s) {}
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& o) noexcept : s_(o.s_) { o.s_ = nullptr; }
    Statement& operator=(Statement&& o) noexcept;

    Statement& bind(int idx, std::int64_t v);
    Statement& bind(int idx, int v) { return bind(idx, static_cast<std::int64_t>(v)); }
    Statement& bind(int idx, double v);
    Statement& bind(int idx, std::string_view v);
    Statement& bind(int idx, const char* v) { return bind(idx, std::string_view(v)); }
    Statement& bind(int idx, const std::string& v) { return bind(idx, std::string_view(v)); }
    Statement& bindNull(int idx);
    template <typename T>
    Statement& bindOpt(int idx, const std::optional<T>& v) {
        return v ? bind(idx, *v) : bindNull(idx);
    }

    // Returns true when a row is available, false when done. Errors return false and set ok()=false.
    bool step();
    bool ok() const { return ok_; }
    const std::string& error() const { return err_; }
    Status run();  // step to completion (for INSERT/UPDATE)
    void reset();

    std::int64_t i64(int col) const;
    double dbl(int col) const;
    std::string text(int col) const;
    bool isNull(int col) const;

private:
    sqlite3_stmt* s_ = nullptr;
    bool ok_ = true;
    std::string err_;
};

// BEGIN IMMEDIATE ... COMMIT; rolls back on destruction unless committed.
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();
    Status commit();
    bool active() const { return active_; }

private:
    Database& db_;
    bool active_ = false;
};

}  // namespace sr::db

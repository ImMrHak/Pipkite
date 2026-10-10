#include "Sqlite.h"

#include "sqlite/sqlite3.h"

namespace sr::db {

Database::~Database() { close(); }

void Database::close() {
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

Status Database::open(const std::string& path, bool readOnly) {
    close();
    const int flags = (readOnly ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)) | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(path.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "out of memory";
        close();
        return failStatus("db_open", "Cannot open database: " + msg);
    }
    sqlite3_busy_timeout(db_, 5000);
    sqlite3_extended_result_codes(db_, 1);
    if (!readOnly) {
        if (auto s = exec("PRAGMA journal_mode=WAL;"); !s) return s;
        // FULL: every critical commit is flushed; pre-submission durability is never skipped.
        if (auto s = exec("PRAGMA synchronous=FULL;"); !s) return s;
    }
    if (auto s = exec("PRAGMA foreign_keys=ON;"); !s) return s;
    return okStatus();
}

Status Database::exec(std::string_view sql) {
    char* err = nullptr;
    std::string q(sql);
    if (sqlite3_exec(db_, q.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        return failStatus("db_exec", msg);
    }
    return okStatus();
}

Result<Statement> Database::prepare(std::string_view sql) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &st, nullptr) != SQLITE_OK) {
        return Error{"db_prepare", std::string(sqlite3_errmsg(db_)) + " in: " + std::string(sql.substr(0, 120))};
    }
    return Statement(st);
}

std::int64_t Database::lastInsertRowId() const { return sqlite3_last_insert_rowid(db_); }
int Database::changes() const { return sqlite3_changes(db_); }
std::string Database::lastError() const { return db_ ? sqlite3_errmsg(db_) : "closed"; }

Statement::~Statement() {
    if (s_) sqlite3_finalize(s_);
}

Statement& Statement::operator=(Statement&& o) noexcept {
    if (this != &o) {
        if (s_) sqlite3_finalize(s_);
        s_ = o.s_;
        o.s_ = nullptr;
        ok_ = o.ok_;
        err_ = std::move(o.err_);
    }
    return *this;
}

Statement& Statement::bind(int idx, std::int64_t v) {
    sqlite3_bind_int64(s_, idx, v);
    return *this;
}
Statement& Statement::bind(int idx, double v) {
    sqlite3_bind_double(s_, idx, v);
    return *this;
}
Statement& Statement::bind(int idx, std::string_view v) {
    sqlite3_bind_text(s_, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    return *this;
}
Statement& Statement::bindNull(int idx) {
    sqlite3_bind_null(s_, idx);
    return *this;
}

bool Statement::step() {
    const int rc = sqlite3_step(s_);
    if (rc == SQLITE_ROW) return true;
    if (rc != SQLITE_DONE) {
        ok_ = false;
        err_ = sqlite3_errmsg(sqlite3_db_handle(s_));
        err_ += " (code " + std::to_string(rc) + ")";
    }
    return false;
}

Status Statement::run() {
    while (step()) {
    }
    if (!ok_) return failStatus("db_step", err_);
    return okStatus();
}

void Statement::reset() {
    sqlite3_reset(s_);
    sqlite3_clear_bindings(s_);
    ok_ = true;
    err_.clear();
}

std::int64_t Statement::i64(int col) const { return sqlite3_column_int64(s_, col); }
double Statement::dbl(int col) const { return sqlite3_column_double(s_, col); }
std::string Statement::text(int col) const {
    const unsigned char* t = sqlite3_column_text(s_, col);
    if (!t) return {};
    return std::string(reinterpret_cast<const char*>(t), static_cast<std::size_t>(sqlite3_column_bytes(s_, col)));
}
bool Statement::isNull(int col) const { return sqlite3_column_type(s_, col) == SQLITE_NULL; }

Transaction::Transaction(Database& db) : db_(db) { active_ = static_cast<bool>(db_.exec("BEGIN IMMEDIATE;")); }

Transaction::~Transaction() {
    if (active_) db_.exec("ROLLBACK;");
}

Status Transaction::commit() {
    if (!active_) return failStatus("db_tx", "Transaction is not active");
    auto s = db_.exec("COMMIT;");
    if (s) active_ = false;
    return s;
}

}  // namespace sr::db

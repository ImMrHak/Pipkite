#include "Schema.h"

namespace sr::db {

namespace {

// Migration 1: the complete V1 schema. Unique constraints on source revisions and command IDs.
const char* kMigration1 = R"SQL(
CREATE TABLE IF NOT EXISTS settings(
  key TEXT PRIMARY KEY, value TEXT NOT NULL, updated_at INTEGER NOT NULL);

CREATE TABLE IF NOT EXISTS sources(
  chat_id INTEGER PRIMARY KEY, title TEXT, chat_type TEXT, binding_json TEXT NOT NULL, updated_at INTEGER NOT NULL);

CREATE TABLE IF NOT EXISTS policies(
  config_version INTEGER PRIMARY KEY, policy_json TEXT NOT NULL, created_at INTEGER NOT NULL);

CREATE TABLE IF NOT EXISTS raw_messages(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  chat_id INTEGER NOT NULL, message_id INTEGER NOT NULL, edit_version INTEGER NOT NULL DEFAULT 0,
  sender_id INTEGER, reply_to INTEGER, published_at INTEGER, received_at INTEGER,
  text TEXT, has_media INTEGER NOT NULL DEFAULT 0, media_kind TEXT,
  historical INTEGER NOT NULL DEFAULT 0,
  UNIQUE(chat_id, message_id, edit_version));
CREATE INDEX IF NOT EXISTS ix_raw_received ON raw_messages(received_at);

CREATE TABLE IF NOT EXISTS signal_revisions(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  chat_id INTEGER NOT NULL, message_id INTEGER NOT NULL, revision INTEGER NOT NULL,
  parser_id TEXT, parser_version TEXT, status TEXT, kind TEXT, state TEXT NOT NULL,
  symbol TEXT, side TEXT, signal_json TEXT NOT NULL, decision_json TEXT, ai_json TEXT,
  timeline_json TEXT, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL,
  UNIQUE(chat_id, message_id, revision));
CREATE INDEX IF NOT EXISTS ix_signal_created ON signal_revisions(created_at);

CREATE TABLE IF NOT EXISTS execution_plans(
  plan_id TEXT PRIMARY KEY, chat_id INTEGER, message_id INTEGER, revision INTEGER,
  account_key TEXT NOT NULL, config_version INTEGER NOT NULL, plan_json TEXT NOT NULL, created_at INTEGER NOT NULL);

CREATE TABLE IF NOT EXISTS commands(
  command_id TEXT PRIMARY KEY,
  plan_id TEXT NOT NULL REFERENCES execution_plans(plan_id),
  child_index INTEGER NOT NULL, action TEXT NOT NULL, symbol TEXT NOT NULL, volume REAL NOT NULL,
  state TEXT NOT NULL, deadline INTEGER NOT NULL, payload_json TEXT NOT NULL,
  request_id TEXT, order_ticket TEXT, filled_volume REAL NOT NULL DEFAULT 0,
  resolved INTEGER NOT NULL DEFAULT 0, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL,
  UNIQUE(plan_id, child_index));
CREATE INDEX IF NOT EXISTS ix_commands_unresolved ON commands(resolved);

CREATE TABLE IF NOT EXISTS command_events(
  id INTEGER PRIMARY KEY AUTOINCREMENT, command_id TEXT NOT NULL REFERENCES commands(command_id),
  from_state TEXT, to_state TEXT NOT NULL, detail_json TEXT, at INTEGER NOT NULL);
CREATE INDEX IF NOT EXISTS ix_command_events_cmd ON command_events(command_id);

CREATE TABLE IF NOT EXISTS broker_orders(
  ticket TEXT PRIMARY KEY, command_id TEXT, symbol TEXT, order_type INTEGER, state TEXT,
  volume_initial REAL, volume_current REAL, price REAL, sl REAL, tp REAL, updated_at INTEGER NOT NULL);

CREATE TABLE IF NOT EXISTS broker_deals(
  ticket TEXT PRIMARY KEY, order_ticket TEXT, position_id TEXT, command_id TEXT, chat_id INTEGER,
  symbol TEXT, deal_type INTEGER, entry INTEGER, volume REAL, price REAL, profit REAL,
  commission REAL, swap REAL, fee REAL, magic INTEGER, time INTEGER, account_key TEXT);
CREATE INDEX IF NOT EXISTS ix_deals_time ON broker_deals(time);

CREATE TABLE IF NOT EXISTS positions(
  position_id TEXT PRIMARY KEY, command_id TEXT, symbol TEXT, side TEXT, volume REAL,
  open_price REAL, sl REAL, tp REAL, profit REAL, closed INTEGER NOT NULL DEFAULT 0, updated_at INTEGER NOT NULL);

CREATE TABLE IF NOT EXISTS reservations(
  command_id TEXT PRIMARY KEY, account_key TEXT NOT NULL, volume REAL NOT NULL, risk_money REAL NOT NULL,
  created_at INTEGER NOT NULL, released_at INTEGER, release_reason TEXT);

CREATE TABLE IF NOT EXISTS session_watermarks(
  scope TEXT PRIMARY KEY, value INTEGER NOT NULL, updated_at INTEGER NOT NULL);

CREATE TABLE IF NOT EXISTS parser_versions(
  parser_id TEXT NOT NULL, version TEXT NOT NULL, origin TEXT NOT NULL, template_json TEXT,
  created_at INTEGER NOT NULL, activated_at INTEGER, active INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(parser_id, version));

CREATE TABLE IF NOT EXISTS parser_fixtures(
  parser_id TEXT NOT NULL, fixture_id TEXT NOT NULL, fixture_json TEXT NOT NULL,
  PRIMARY KEY(parser_id, fixture_id));

CREATE TABLE IF NOT EXISTS ai_assessments(
  id INTEGER PRIMARY KEY AUTOINCREMENT, chat_id INTEGER, message_id INTEGER, revision INTEGER,
  assessment_json TEXT NOT NULL, created_at INTEGER NOT NULL);

CREATE TABLE IF NOT EXISTS update_history(
  id INTEGER PRIMARY KEY AUTOINCREMENT, release_id TEXT NOT NULL, component TEXT NOT NULL,
  from_version TEXT, to_version TEXT, state TEXT NOT NULL, notes_json TEXT,
  restart_required INTEGER NOT NULL DEFAULT 0, at INTEGER NOT NULL, notes_shown INTEGER NOT NULL DEFAULT 0);

CREATE TABLE IF NOT EXISTS profit_snapshots(
  time INTEGER NOT NULL, account_key TEXT NOT NULL, balance REAL, equity REAL, floating REAL,
  PRIMARY KEY(time, account_key));

CREATE TABLE IF NOT EXISTS account_bindings(
  account_key TEXT PRIMARY KEY, terminal_id TEXT NOT NULL, login INTEGER NOT NULL, server TEXT NOT NULL,
  approved_at INTEGER NOT NULL, active INTEGER NOT NULL DEFAULT 1);
)SQL";

}  // namespace

int currentSchemaVersion(Database& db) {
    auto st = db.prepare("SELECT COALESCE(MAX(version),0) FROM migrations");
    if (!st) return 0;
    return st.value().step() ? static_cast<int>(st.value().i64(0)) : 0;
}

Status migrate(Database& db, std::int64_t nowMs) {
    if (auto s = db.exec("CREATE TABLE IF NOT EXISTS migrations(version INTEGER PRIMARY KEY, applied_at INTEGER NOT NULL, description TEXT);"); !s) return s;
    const int current = currentSchemaVersion(db);
    if (current > kSchemaVersion)
        return failStatus("schema_newer", "The local database was created by a newer Pipkite (schema " + std::to_string(current) + "). Install that version or restore a compatible backup.");
    if (current < 1) {
        Transaction tx(db);
        if (auto s = db.exec(kMigration1); !s) return s;
        auto st = db.prepare("INSERT INTO migrations(version, applied_at, description) VALUES(1, ?, 'initial V1 schema')");
        if (!st) return st.error();
        st.value().bind(1, nowMs);
        if (auto s = st.value().run(); !s) return s;
        if (auto s = tx.commit(); !s) return s;
    }
    return okStatus();
}

}  // namespace sr::db

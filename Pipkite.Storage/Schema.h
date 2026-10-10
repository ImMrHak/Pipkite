#pragma once

#include "Sqlite.h"

namespace sr::db {

inline constexpr int kSchemaVersion = 1;

// Applies pending migrations inside transactions. Refuses to open a database created by a
// newer schema instead of guessing (executable rollback after irreversible migration is not claimed).
Status migrate(Database& db, std::int64_t nowMs);
int currentSchemaVersion(Database& db);

}  // namespace sr::db

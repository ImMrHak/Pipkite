#pragma once
// Local backup/export/import. Consistent live snapshots use SQLite's online backup API in
// incremental steps (never a raw copy of the main file while WAL writes continue).
//
// Archive format (.srbackup, versioned):
//   "SRBK" | u32 formatVersion | u32 manifestLength | manifest JSON | entries...
//   entry = u32 nameLength | name | u64 dataLength | data
// Manifest lists app/schema versions, entry names, sizes and SHA-256 checksums.
// Optional password protection wraps the whole archive in PBKDF2 + AES-256-GCM ("SRBE").
// Secrets (Telegram session, DPAPI keys, pairing secrets, licenses) are never included.

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "Pipkite.Core/Result.h"
#include "Store.h"

namespace sr::db {

enum class ExportScope { ConfigurationOnly, FullHistory };

struct BackupResult {
    std::wstring path;
    std::uint64_t bytes = 0;
    nlohmann::json manifest;
};

// Snapshot the live database into destDbPath using sqlite3_backup_step in small increments.
Status onlineSnapshot(Store& store, const std::wstring& destDbPath, int pagesPerStep = 64);

Result<BackupResult> createBackup(Store& store, const std::wstring& archivePath, ExportScope scope, const std::string& password, const nlohmann::json& configJson);

struct ImportPreview {
    nlohmann::json manifest;
    std::wstring stagedDbPath;   // validated snapshot in a temporary staging directory
    nlohmann::json configJson;   // configuration/parsers payload (if present)
    nlohmann::json counts;       // row counts found in the snapshot
    std::string integrity;
};

// Stages and validates an archive: size/path limits, checksums, schema version, integrity and FK checks.
// Executable/plugin content is rejected. Nothing is applied here.
Result<ImportPreview> stageImport(const std::wstring& archivePath, const std::string& password, const std::wstring& stagingDir);

// CSV/JSON analysis report of signals and deals (no secrets).
Status exportReport(Store& store, const std::wstring& path, bool csv);

}  // namespace sr::db

#include "Backup.h"

#include <windows.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>

#include "Schema.h"
#include "SignalRelay.Core/Models.h"
#include "SignalRelay.Protocol/Crypto.h"
#include "SignalRelay.Protocol/Platform.h"
#include "sqlite/sqlite3.h"

namespace sr::db {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kFormatVersion = 1;
constexpr std::uint64_t kMaxArchive = 2ull * 1024 * 1024 * 1024;  // 2 GiB
constexpr std::uint64_t kMaxEntry = 1536ull * 1024 * 1024;
constexpr std::uint32_t kMaxManifest = 1024 * 1024;

void putU32(std::string& s, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void putU64(std::string& s, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
bool getU32(const std::string& s, std::size_t& pos, std::uint32_t& v) {
    if (pos + 4 > s.size()) return false;
    v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(static_cast<unsigned char>(s[pos + i])) << (8 * i);
    pos += 4;
    return true;
}
bool getU64(const std::string& s, std::size_t& pos, std::uint64_t& v) {
    if (pos + 8 > s.size()) return false;
    v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(static_cast<unsigned char>(s[pos + i])) << (8 * i);
    pos += 8;
    return true;
}

bool safeEntryName(const std::string& n) {
    static const std::set<std::string> allowed{"database.sqlite", "config.json"};
    return allowed.count(n) > 0;  // fixed names only: no paths, no executables, no plugins
}

std::string bytesToString(const crypto::Bytes& b) { return std::string(b.begin(), b.end()); }

}  // namespace

Status onlineSnapshot(Store& store, const std::wstring& destDbPath, int pagesPerStep) {
    std::error_code ec;
    fs::remove(destDbPath, ec);
    sqlite3* dest = nullptr;
    const std::string destUtf8 = platform::narrow(destDbPath);
    if (sqlite3_open_v2(destUtf8.c_str(), &dest, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        if (dest) sqlite3_close(dest);
        return failStatus("backup_open", "Cannot create snapshot file");
    }
    Status result = okStatus();
    store.read([&](Database& src) {
        sqlite3_backup* b = sqlite3_backup_init(dest, "main", src.handle(), "main");
        if (!b) {
            result = failStatus("backup_init", sqlite3_errmsg(dest));
            return 0;
        }
        int rc;
        do {
            rc = sqlite3_backup_step(b, pagesPerStep);
            if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) Sleep(5);
        } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);
        sqlite3_backup_finish(b);
        if (rc != SQLITE_DONE) result = failStatus("backup_step", sqlite3_errmsg(dest));
        return 0;
    });
    sqlite3_close(dest);
    return result;
}

Result<BackupResult> createBackup(Store& store, const std::wstring& archivePath, ExportScope scope, const std::string& password, const json& configJson) {
    const std::wstring tmpDir = platform::ensureSubdir(L"staging");
    const std::wstring snap = tmpDir + L"\\snapshot-" + std::to_wstring(GetTickCount64()) + L".sqlite";
    std::string dbBytes;
    if (scope == ExportScope::FullHistory) {
        if (auto st = onlineSnapshot(store, snap); !st) return st.error();
        // Remove machine-bound data from the snapshot copy: account bindings are revalidated on restore.
        {
            Database d;
            if (auto st = d.open(platform::narrow(snap)); !st) return st.error();
            d.exec("DELETE FROM account_bindings; DELETE FROM settings WHERE key LIKE 'secret.%' OR key LIKE 'telegram.%';");
            d.exec("PRAGMA journal_mode=DELETE;");
        }
        auto data = platform::readFileUtf8(snap, kMaxEntry);
        std::error_code ec;
        fs::remove(snap, ec);
        if (!data) return Error{"backup_read", "Snapshot is too large or unreadable"};
        dbBytes = std::move(*data);
    }
    const std::string cfg = configJson.dump(2);

    json manifest = {{"format", "signalrelay-backup"},
                     {"formatVersion", kFormatVersion},
                     {"appVersion", kProductVersion},
                     {"schemaVersion", kSchemaVersion},
                     {"scope", scope == ExportScope::FullHistory ? "full" : "configuration"},
                     {"createdAtMs", static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())},
                     {"excluded", {"telegram session", "verification codes", "broker credentials", "DPAPI keys", "pairing secrets", "machine-bound licenses"}}};
    json entries = json::array();
    auto addEntry = [&](const std::string& name, const std::string& data) {
        entries.push_back({{"name", name}, {"size", data.size()}, {"sha256", crypto::toHex(crypto::sha256(data))}});
    };
    addEntry("config.json", cfg);
    if (!dbBytes.empty()) addEntry("database.sqlite", dbBytes);
    manifest["entries"] = entries;

    std::string archive = "SRBK";
    putU32(archive, kFormatVersion);
    const std::string man = manifest.dump();
    putU32(archive, static_cast<std::uint32_t>(man.size()));
    archive += man;
    auto writeEntry = [&](const std::string& name, const std::string& data) {
        putU32(archive, static_cast<std::uint32_t>(name.size()));
        archive += name;
        putU64(archive, data.size());
        archive += data;
    };
    writeEntry("config.json", cfg);
    if (!dbBytes.empty()) writeEntry("database.sqlite", dbBytes);

    std::string out;
    if (!password.empty()) {
        auto box = crypto::sealWithPassword(crypto::Bytes(archive.begin(), archive.end()), password);
        if (!box) return Error{"backup_encrypt", "Encryption failed (password must be at least 8 characters)"};
        out = "SRBE";
        putU32(out, 1);
        putU32(out, box->iterations);
        out += bytesToString(box->salt);   // 16
        out += bytesToString(box->nonce);  // 12
        out += bytesToString(box->tag);    // 16
        putU64(out, box->ciphertext.size());
        out += bytesToString(box->ciphertext);
    } else {
        out = std::move(archive);
    }
    if (!platform::writeFileAtomic(archivePath, out)) return Error{"backup_write", "Cannot write the backup archive"};
    BackupResult r;
    r.path = archivePath;
    r.bytes = out.size();
    r.manifest = manifest;
    return r;
}

Result<ImportPreview> stageImport(const std::wstring& archivePath, const std::string& password, const std::wstring& stagingDir) {
    std::error_code ec;
    const auto size = fs::file_size(archivePath, ec);
    if (ec) return Error{"import_read", "Cannot read the archive"};
    if (size > kMaxArchive) return Error{"import_too_large", "Archive exceeds the 2 GiB limit"};
    auto dataOpt = platform::readFileUtf8(archivePath, kMaxArchive);
    if (!dataOpt) return Error{"import_read", "Cannot read the archive"};
    std::string data = std::move(*dataOpt);

    if (data.size() >= 4 && data.compare(0, 4, "SRBE") == 0) {
        if (password.empty()) return Error{"import_password", "This backup is password protected"};
        std::size_t pos = 4;
        std::uint32_t ver = 0, iters = 0;
        std::uint64_t clen = 0;
        if (!getU32(data, pos, ver) || ver != 1 || !getU32(data, pos, iters) || iters < 100000 || iters > 10000000) return Error{"import_corrupt", "Encrypted header is invalid"};
        if (pos + 44 > data.size()) return Error{"import_corrupt", "Encrypted header is truncated"};
        crypto::SealedBox box;
        box.iterations = iters;
        box.salt.assign(data.begin() + pos, data.begin() + pos + 16);
        box.nonce.assign(data.begin() + pos + 16, data.begin() + pos + 28);
        box.tag.assign(data.begin() + pos + 28, data.begin() + pos + 44);
        pos += 44;
        if (!getU64(data, pos, clen) || pos + clen != data.size()) return Error{"import_corrupt", "Encrypted payload length mismatch"};
        box.ciphertext.assign(data.begin() + static_cast<std::ptrdiff_t>(pos), data.end());
        auto plain = crypto::openWithPassword(box, password);
        if (!plain) return Error{"import_password", "Wrong password or the archive was modified"};
        data.assign(plain->begin(), plain->end());
    }

    if (data.size() < 12 || data.compare(0, 4, "SRBK") != 0) return Error{"import_format", "Not a SignalRelay backup archive"};
    std::size_t pos = 4;
    std::uint32_t fmt = 0, manLen = 0;
    if (!getU32(data, pos, fmt) || fmt != kFormatVersion) return Error{"import_format", "Unsupported backup format version"};
    if (!getU32(data, pos, manLen) || manLen > kMaxManifest || pos + manLen > data.size()) return Error{"import_corrupt", "Manifest is invalid"};
    json manifest = json::parse(data.substr(pos, manLen), nullptr, false);
    pos += manLen;
    if (manifest.is_discarded() || manifest.value("format", "") != "signalrelay-backup") return Error{"import_corrupt", "Manifest is not valid JSON"};
    const int schema = manifest.value("schemaVersion", 0);
    if (schema < 1 || schema > kSchemaVersion) return Error{"import_schema", "Backup schema " + std::to_string(schema) + " is not supported by this version"};

    std::map<std::string, std::string> expected;
    for (auto& e : manifest.value("entries", json::array())) expected[e.value("name", "")] = e.value("sha256", "");

    ImportPreview pv;
    pv.manifest = manifest;
    fs::create_directories(stagingDir, ec);
    std::set<std::string> seen;
    while (pos < data.size()) {
        std::uint32_t nameLen = 0;
        std::uint64_t len = 0;
        if (!getU32(data, pos, nameLen) || nameLen == 0 || nameLen > 128 || pos + nameLen > data.size()) return Error{"import_corrupt", "Entry header is invalid"};
        std::string name = data.substr(pos, nameLen);
        pos += nameLen;
        if (!safeEntryName(name)) return Error{"import_rejected", "Archive contains an unexpected entry '" + name + "'; executable or plugin content is never imported this way"};
        if (!seen.insert(name).second) return Error{"import_corrupt", "Duplicate entry " + name};
        if (!getU64(data, pos, len) || len > kMaxEntry || pos + len > data.size()) return Error{"import_corrupt", "Entry size is invalid"};
        std::string content = data.substr(pos, static_cast<std::size_t>(len));
        pos += static_cast<std::size_t>(len);
        if (crypto::toHex(crypto::sha256(content)) != expected[name]) return Error{"import_checksum", "Checksum mismatch for " + name};
        if (name == "config.json") {
            pv.configJson = json::parse(content, nullptr, false);
            if (pv.configJson.is_discarded()) return Error{"import_corrupt", "config.json is not valid JSON"};
        } else if (name == "database.sqlite") {
            pv.stagedDbPath = stagingDir + L"\\import.sqlite";
            if (!platform::writeFileAtomic(pv.stagedDbPath, content)) return Error{"import_stage", "Cannot write staging file"};
        }
    }
    for (auto& [n, h] : expected)
        if (!seen.count(n)) return Error{"import_corrupt", "Manifest entry missing from archive: " + n};

    if (!pv.stagedDbPath.empty()) {
        Database d;
        if (auto st = d.open(platform::narrow(pv.stagedDbPath), true); !st) return st.error();
        auto q = d.prepare("PRAGMA integrity_check");
        if (!q) return q.error();
        while (q.value().step()) pv.integrity += q.value().text(0);
        if (pv.integrity != "ok") return Error{"import_integrity", "Database integrity check failed: " + pv.integrity};
        auto fk = d.prepare("PRAGMA foreign_key_check");
        if (fk && fk.value().step()) return Error{"import_integrity", "Database has foreign-key violations"};
        if (currentSchemaVersion(d) > kSchemaVersion) return Error{"import_schema", "Database schema is newer than this application"};
        for (const char* t : {"signal_revisions", "commands", "broker_deals", "parser_versions", "sources"}) {
            auto c = d.prepare(std::string("SELECT COUNT(*) FROM ") + t);
            pv.counts[t] = (c && c.value().step()) ? c.value().i64(0) : 0;
        }
        auto un = d.prepare("SELECT COUNT(*) FROM commands WHERE resolved=0");
        pv.counts["unresolvedCommandsAsEvidenceOnly"] = (un && un.value().step()) ? un.value().i64(0) : 0;
    }
    return pv;
}

Status exportReport(Store& store, const std::wstring& path, bool csv) {
    auto rows = store.historyPage(HistoryFilter{}, 0, 1000000);
    auto deals = store.loadDeals(0);
    std::string out;
    if (csv) {
        auto esc = [](std::string s) {
            std::string r = "\"";
            for (char c : s) {
                if (c == '"') r += "\"\"";
                else if (c == '\n' || c == '\r') r += ' ';
                else r += c;
            }
            return r + "\"";
        };
        out = "time_ms,source,chat_id,message_id,revision,state,status,symbol,side,parser,reason\n";
        for (auto& r : rows) {
            out += std::to_string(r.value("at", static_cast<std::int64_t>(0))) + "," + esc(r.value("source", "")) + "," + r.value("chatId", "") + "," + r.value("messageId", "") + "," +
                   std::to_string(r.value("revision", 0)) + "," + r.value("state", "") + "," + r.value("status", "") + "," + r.value("symbol", "") + "," + r.value("side", "") + "," +
                   esc(r.value("parser", "")) + "," + esc(r.value("reason", "")) + "\n";
        }
        out += "\ndeal_ticket,time_ms,symbol,type,entry,volume,price,profit,commission,swap,fee,magic,command_id\n";
        for (auto& d : deals) {
            out += std::to_string(d.ticket) + "," + std::to_string(d.timeMs) + "," + d.symbol + "," + std::to_string(d.type) + "," + std::to_string(d.entry) + "," +
                   std::to_string(d.volume) + "," + std::to_string(d.price) + "," + std::to_string(d.profit) + "," + std::to_string(d.commission) + "," + std::to_string(d.swap) + "," +
                   std::to_string(d.fee) + "," + std::to_string(d.magic) + "," + d.commandId + "\n";
        }
    } else {
        json j;
        j["signals"] = rows;
        json ds = json::array();
        for (auto& d : deals) ds.push_back({{"ticket", std::to_string(d.ticket)}, {"timeMs", d.timeMs}, {"symbol", d.symbol}, {"type", d.type}, {"entry", d.entry}, {"volume", d.volume},
                                            {"price", d.price}, {"profit", d.profit}, {"commission", d.commission}, {"swap", d.swap}, {"fee", d.fee}, {"commandId", d.commandId}});
        j["deals"] = ds;
        j["exportedBy"] = std::string(kProductName) + " " + kProductVersion;
        out = j.dump(2);
    }
    if (!platform::writeFileAtomic(path, out)) return failStatus("export_write", "Cannot write the report file");
    return okStatus();
}

}  // namespace sr::db

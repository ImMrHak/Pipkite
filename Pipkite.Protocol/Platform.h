#pragma once
// Windows platform helpers: per-user paths, SID, string conversion, DPAPI-protected secrets.

#include <optional>
#include <string>
#include <string_view>

#include "Crypto.h"

namespace sr::platform {

std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view wide);

std::wstring currentUserSid();
// %LOCALAPPDATA%\Pipkite (created on demand). Override with PIPKITE_DATA_DIR for tests/portable runs.
std::wstring dataDir();
std::wstring ensureSubdir(const std::wstring& name);
std::wstring exeDir();

// 32-byte random secret stored under dataDir\secrets\<name>.bin, protected with current-user DPAPI.
std::optional<crypto::Bytes> loadOrCreateSecret(const std::wstring& name);
std::optional<crypto::Bytes> loadSecret(const std::wstring& name);
bool deleteSecret(const std::wstring& name);

// Optional PIPKITE_INSTANCE isolates side-by-side instances (integration tests, portable runs).
std::wstring instanceSuffix();
// Per-user pipe names; never global writable names.
std::wstring pipeName(const std::wstring& suffix);

std::optional<std::string> readFileUtf8(const std::wstring& path, std::size_t maxBytes = 64 * 1024 * 1024);
bool writeFileAtomic(const std::wstring& path, std::string_view data);

std::string machineName();

}  // namespace sr::platform

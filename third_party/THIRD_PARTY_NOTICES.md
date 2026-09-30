# Third-party notices

| Component | Version / pin | License | Use |
|---|---|---|---|
| SQLite amalgamation | 3.53.4 (`sqlite-amalgamation-3530400`, sha256 of sqlite3.c `b1dd5d74ec7f29055a6684fa06fb3c2f6821c87dd38f9a458dfd2e8a1db28189`) | Public domain | Local database (compiled in) |
| nlohmann/json | 3.12.0 (json.hpp sha256 `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`) | MIT (`licenses/nlohmann-json-LICENSE.MIT.txt`) | JSON (header-only, compiled in) |
| Microsoft Visual C++ runtime (vcruntime140) | from Visual Studio 2026 (MSVC 14.51) | Microsoft Visual C++ Redistributable license | App-local runtime for the C++/CLI UI |
| .NET Framework 4.8 | OS component | Microsoft | UI runtime (not redistributed) |
| TDLib (`tdjson.dll`) | built from source by `tools/build-tdlib.ps1`; exact version/commit in `licenses/TDLib-VERSION.txt` | Boost Software License 1.0 (`licenses/TDLib-LICENSE_1_0.txt`) | Telegram client (shipped) |
| OpenSSL 3 (`libcrypto-3-x64.dll`, `libssl-3-x64.dll`) | vcpkg `openssl:x64-windows` | Apache License 2.0 (`licenses/OpenSSL-LICENSE.txt`) | TDLib dependency (shipped) |
| zlib (`z.dll`) | vcpkg `zlib:x64-windows` | zlib license (`licenses/zlib-LICENSE.txt`) | TDLib dependency (shipped) |
| Spring Boot | 4.1.1 (backend only) | Apache License 2.0 | AI backend |
| Apache Maven Wrapper | 3.3.4 / Maven 3.9.16 (backend build only) | Apache License 2.0 | Backend build |

Windows CNG, DPAPI, WinHTTP and named pipes are operating-system APIs. MetaTrader 5 and MQL5 are
products of MetaQuotes; SignalRelay ships its own EA (source and compiled .ex5) and a bridge DLL that it
installs into the user's own terminal.

Before redistributing a release, re-check each license and record exact dependency versions/commits in
the release manifest.

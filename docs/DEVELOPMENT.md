# Development

## Requirements

- Windows 10/11 x64
- Visual Studio 2026 with *Desktop development with C++*, *C++/CLI support* and the *.NET Framework 4.8 targeting pack*
- Optional: Inno Setup (installer), MetaTrader 5 (to compile/run the EA), Node 22 (website)

## Build and test

```powershell
MSBuild Pipkite.sln -p:Configuration=Release -p:Platform=x64 -m
x64\Release\Pipkite.Tests.exe          # all tests
x64\Release\Pipkite.Tests.exe e2e      # only tests whose name contains "e2e"
x64\Release\Pipkite.Benchmarks.exe     # speed numbers for your machine
```

In Visual Studio: open `Pipkite.sln`, pick Release/Debug x64, F5. The UI starts the engine by itself.
The build signs the sample parser and the bridge with a local dev key (`%LOCALAPPDATA%\Pipkite\devkeys`).

## Test terminal (tests only)

`Pipkite.FakeTerminal` stands in for MetaTrader in the end-to-end tests. It loads the real bridge DLL and talks to
it like the EA, with an in-memory broker. It is never shipped with the app. To try the UI against it, use an
isolated data folder so your real install isn't touched:

```powershell
$env:PIPKITE_DATA_DIR = "$env:TEMP\sr-dev"; $env:PIPKITE_INSTANCE = "dev"
x64\Release\Pipkite.exe
x64\Release\Pipkite.FakeTerminal.exe --login 90000001    # then approve pairing with 90000001
```


## Other pieces

- **EA:** `powershell -File mql5\compile-ea.ps1 -OutDir x64\Release\mql5` (needs MetaTrader 5 installed).
- **TDLib (Telegram):** `tools\build-tdlib.ps1` builds it into `third_party\tdlib\`, copied next to the binaries on build.
- **Release package:** `installer\package.ps1 -Build` (portable zip, plus setup.exe with Inno Setup).
  `tools\build-release.ps1` does a clean signed release build + tests + package.
- **Website:** see `frontend/` (`npm install`, `npm run dev`).

## How it fits together

```
Telegram (TDLib) -> Engine -> parser -> rules/planner -> journal (SQLite) -> named pipe -> MT5BridgeHost.dll -> EA -> broker
                       \-> backend (license, AI review, parser updates)
UI (WinForms) <-> Engine over a local named pipe
```

- The engine is native C++ and does all the work; the UI only displays and sends commands.
- Every order is written to the journal before it's sent, so a crash never sends twice.
- Parsers are signed modules in `plugins\<id>\<version>\`, verified and hot-reloaded.
- `Pipkite.Plugin.GoldParser` is a sample parser module, handy for tests.

## Rules for changes

- Run the tests before a pull request.
- Never commit keys, `*.key`, `var/`, or local data.
- Keep the UI text plain and short.

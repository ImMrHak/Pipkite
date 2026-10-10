# SignalRelay

Copy trading signals from Telegram to MetaTrader 5, automatically and fast (about 4 ms from message to order).
Free for everyone, including the AI review of every signal.

![Dashboard](docs/images/dashboard.png)

## What it does

- Reads new messages from the Telegram channels and groups you pick (your own account, private channels work).
- Each channel gets a parser built for its signal format. You request it from the app, it arrives as an update.
- Checks every signal against your rules: lot size or risk %, max spread, trading hours, entry ranges.
- Places the order in your MT5 terminal through the SignalRelay EA.
- AI review on every signal: Good, Uncertain or Bad, with the reasons.
- Pause / resume copying with one click. Pausing never closes open trades.

## Get it

1. Download the installer from the website (or build it, see below). Windows 10/11, 64-bit.
2. Get a free license key on the website. It unlocks parsers, AI review and updates.
3. Follow the setup wizard: Telegram, MetaTrader 5, your channels, a test message.

Use a demo account first. No copier can guarantee fills or profits.

User guide: [docs/USER-GUIDE.md](docs/USER-GUIDE.md)

## Build from source

You need Visual Studio 2026 (C++ desktop + C++/CLI + .NET Framework 4.8 targeting pack).

```powershell
# app, engine, EA bridge, tests (Release x64)
MSBuild SignalRelay.sln -p:Configuration=Release -p:Platform=x64 -m
x64\Release\SignalRelay.Tests.exe

# installer + portable zip (needs Inno Setup)
powershell -ExecutionPolicy Bypass -File installer\package.ps1 -Build
```

Or open `SignalRelay.sln` and press F5. More in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Repo layout

| Folder | What's in it |
|---|---|
| `SignalRelay.WinForms` | the desktop UI |
| `SignalRelay.Engine` | the engine: Telegram, parsing, decisions, orders, updates |
| `SignalRelay.Core` | parser, rules, planner, AI client |
| `SignalRelay.Storage` | local SQLite database and backups |
| `SignalRelay.Protocol` | IPC, crypto, signing |
| `SignalRelay.MT5BridgeHost` / `MT5BridgeImpl` | the DLL the EA talks to |
| `mql5/` | the MetaTrader 5 EA |
| `SignalRelay.Tests` | tests (unit + end-to-end with a fake MT5) |
| `frontend/` | the website |

## Contributing

Issues and pull requests are welcome. Keep changes small, run the tests before sending, and don't commit keys,
secrets or local data.

## License

MIT, see [LICENSE](LICENSE). Third-party notices: [third_party/THIRD_PARTY_NOTICES.md](third_party/THIRD_PARTY_NOTICES.md).

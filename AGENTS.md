# AGENTS.md: working on Pipkite with an AI assistant

This file is for AI coding assistants (Claude Code, Codex, Cursor, Copilot, ...) and the people driving them.
Read all of it before you change anything. Humans: the same rules apply to you, and
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) has more background.

Step-by-step recipes for common changes are in
[.claude/skills/pipkite-contribute/SKILL.md](.claude/skills/pipkite-contribute/SKILL.md). Claude Code loads it as
the `pipkite-contribute` skill; other tools should open the file.

## What Pipkite is

A Windows app that copies trading signals from Telegram channels to MetaTrader 5. It places **real orders with
real money** for people who trust it. A wrong number, a doubled order or a missed stop loss costs them money.
Correct and boring beats clever, every time.

```
Telegram (TDLib) -> Engine -> parser -> rules/planner -> journal (SQLite) -> named pipe -> MT5BridgeHost.dll -> EA -> broker
UI (WinForms, C++/CLI) <-> Engine over a local named pipe (JSON methods, 64 KiB frames)
```

| Folder | What's in it |
|---|---|
| `Pipkite.Core` | parser (`SignalParser`, `ParserTemplate`, `ParserBuilder`), rules (`Policy`), `Planner`, `StateMachine`, `Dedup`, AI contract. Pure C++: no files, network, threads or Windows calls. |
| `Pipkite.Engine` | the engine process: Telegram, plugins, updater, journal, UI methods (`EngineUi.cpp`) |
| `Pipkite.Storage` | SQLite database, schema migrations (`Schema.cpp`), backups |
| `Pipkite.Protocol` | named pipes, framing, crypto, signature checks |
| `Pipkite.MT5BridgeHost` / `MT5BridgeImpl` | the DLL the MetaTrader EA calls |
| `mql5/PipkiteEA` | the MetaTrader 5 EA: `.mq5` source **and** the compiled `.ex5`, both in git |
| `Pipkite.WinForms` | the desktop UI (C++/CLI, one header per page) |
| `Pipkite.Plugin.Template`, `Pipkite.Plugin.GoldParser` | parser modules (C ABI in `include/PipkitePluginAbi.h`) |
| `Pipkite.Tests` | all tests: unit, format corpus, end-to-end with a test terminal |
| `Pipkite.FakeTerminal` | stand-in MetaTrader for tests and screenshots, never shipped |
| `Pipkite.Benchmarks` | speed numbers (message to order is about 4 ms; keep it there) |
| `frontend/` | the website (Astro + Tailwind, English/French/Spanish/Arabic) |
| `config/templates/`, `Pipkite.Tests/fixtures/` | **generated** JSON; never edit by hand (see the skill) |
| `tools/`, `installer/` | build, sign, package scripts (maintainers) |
| `third_party/` | vendored libraries; don't edit them |

The backend (licenses, AI review, parser delivery) is **private and not in this repo**. Don't guess its code or
invent endpoints. If a change needs the backend, say so in an issue.

## Build and test

Windows 10/11 x64, Visual Studio 2026 (C++ desktop + C++/CLI + .NET Framework 4.8 targeting pack).

```powershell
MSBuild Pipkite.sln -p:Configuration=Release -p:Platform=x64 -m
x64\Release\Pipkite.Tests.exe              # every test; the last line must say "0 failed"
x64\Release\Pipkite.Tests.exe planner      # only tests whose name contains "planner"
```

- The end-to-end test prints `(skipped: ...)` and passes if the engine or FakeTerminal wasn't built. A skip is not
  a pass: build the whole solution so it really runs.
- `LNK1104` on link = a `Pipkite.exe`, `Pipkite.Engine.exe` or `Pipkite.FakeTerminal.exe` is still running. Close
  only those. **Never kill `terminal64.exe`**: that is the user's real MetaTrader.
- Website: `cd frontend; npm install; npm run build` (it type-checks; a missing translation fails the build).

## Hard rules (break one and the change is rejected)

1. **Never touch real accounts or real data.** No live MetaTrader, no real Telegram account, no real
   `%LOCALAPPDATA%\Pipkite`. Always set an isolated instance **in the same shell, before starting anything**:
   ```powershell
   $env:PIPKITE_DATA_DIR = "$env:TEMP\pk-dev"; $env:PIPKITE_INSTANCE = "dev"
   x64\Release\Pipkite.exe
   x64\Release\Pipkite.FakeTerminal.exe --login 90000001     # then approve pairing for 90000001
   ```
   Without these two variables the app, the test terminal and `PIPKITE_DEMO_SEED` talk to the user's **real**
   install: the demo seed would approve a terminal and switch on automatic trading there.
2. **Never delete** `%LOCALAPPDATA%\PipkitePublisher\devkeys` (the signing key; lost = installed apps reject new
   parsers), `trusted_keys/`, or the user's data folder. Don't run `git clean`, `git reset --hard`,
   `git push --force` or delete untracked folders without the owner's explicit OK.
3. **No secrets in git.** No `*.key`, `devkeys/`, `.env`, `var/`, databases, license keys, Telegram api_id/api_hash,
   phone numbers or real channel messages with names. Not in code, tests, fixtures, logs, commits or screenshots.
4. **Stage files by name.** Never `git add -A` / `git add .`: the working copy has private, untracked folders
   (`backend-admin/`, `frontend-admin/`, `tools/server/`, `.env`, ...) that must never be committed or pushed.
5. **No trade without all of symbol, side, entry (or NOW), stop loss and take profit, read with no conflict.**
   Never fill in a missing value, guess a default, or loosen a check to make a message "work". Unsure = not a
   signal (review), never a trade.
6. **Journal before send.** Every order is written to the journal before it reaches the bridge, so a crash never
   sends twice. Don't add any path around it, and don't weaken dedup or the state machine.
7. **Pausing never closes trades.** Don't make pause, errors or shutdown close, modify or open positions.
8. **Signing stays intact.** Parsers, the bridge and the update manifest are signature-checked. No bypasses, no
   "skip verify in debug", no new trusted keys without the maintainers.
9. **No fake or simulated modes in the app or website.** Test-only behavior goes behind `PIPKITE_TEST_HOOKS=1`
   (see `test.injectMessage`) and does nothing otherwise.
10. **A channel uses a format only after the user picked it, and trades only after the user's OK.** Don't
    auto-assign formats or parsers to channels, and never set `confirmedFormat` anywhere but
    `source.confirmFormat` (see "Waiting for the user's OK" in docs/DEVELOPMENT.md).
11. **AI never decides a trade.** AI only suggests format words, checked by `applyFormatSuggestion`; nothing on the
    signal path waits for or calls an AI provider. Messages sent to any AI or the backend are redacted first.
12. **Tests pass before every pull request**, and new logic comes with a test that fails without your change.
13. **GPL-3.0.** Everything added is GPL-3.0. Don't paste code under an incompatible license. New third-party code
    goes in `third_party/` with an entry in `third_party/THIRD_PARTY_NOTICES.md`.

## Danger zones

Changes here can lose people money or break every install. Touch them only when the task needs it, keep the diff
minimal, add tests for both the old and the new behavior, and list them in the pull request.

| Area | Why it's dangerous |
|---|---|
| `Core/SignalParser.cpp`, `ParserTemplate.cpp` | a misread price becomes a real order; change a built-in format and you must bump its `version` |
| `Core/FormatAssist.cpp`, `ai.suggestFormat`, shared formats in `EngineUi.cpp` | text from an AI or another user becomes rules: only the allowed fields, always checked |
| `Engine::evaluate`, `savePolicy` (format OK) | the one place that holds back trades of a new format |
| `Core/Planner.cpp`, `Policy.cpp` | lot size, SL/TP, entry ranges, spread and time rules |
| `Core/StateMachine.cpp`, `Dedup.cpp`, the journal in `Engine.cpp` | double orders, lost orders after a crash |
| `MT5BridgeImpl`, `MT5BridgeHost`, `mql5/PipkiteEA` | the wire to the broker; old EAs stay installed after an update |
| `include/PipkitePluginAbi.h` | changing a struct or the ABI number breaks every installed parser |
| `Storage/Schema.cpp` | existing databases on users' PCs; see "Database" in the skill |
| `Protocol/Signing.cpp`, `Engine/Updater.cpp`, `PluginManager.cpp`, `trusted_keys/` | security of what code runs |

## How the code is written

- C++20, MSVC, warning level 4, `/utf-8`. Namespace `sr`. 4-space indent, `camelCase` functions, `PascalCase`
  types, trailing `_` on members. Match the file you're in.
- Files are UTF-8 without BOM and LF line endings. Keep it that way; don't let an editor re-encode or re-indent a
  whole file. Keep `PipkiteEA.mq5` plain ASCII.
- Change only what the task needs. No drive-by renames, reformatting, refactors, dependency updates or "cleanup"
  in the same change.
- Reuse what exists: `Result.h` (`Result<T>`, `Status`, `failStatus`), `Text.h`, `IClock` (never read the system
  clock in new Core code; take the clock so tests can use `FakeClock`), the UI's `J::`, `L::` and `Theme::`.
- Errors are values (`Result`/`Status`) in Core and Storage. Don't swallow them; return or show them.
- The engine owns all state. The UI only shows things and calls engine methods; it never touches the database,
  Telegram or MetaTrader.
- Nothing slow on the signal path (Telegram message to order): no network, disk scans or big allocations.
  If you touch the parser or planner, run `x64\Release\Pipkite.Benchmarks.exe` before and after.
- Comments say *why*, briefly. No commented-out code, no TODOs without an issue.
- Don't bump version numbers or edit release files. Releases are done by the maintainers.

## UI and text

- Plain, short English. Say what happens, not how ("Pausing never closes open trades.").
- Every main button gets a tooltip (`L::Btn(text, tag, help)` or `L::Tip`). Owner-drawn controls need an
  accessible object (`OwnerDrawnAccessible` in `Controls.h`).
- Must look right at 1024x640, at 125–150% display scaling, in dark and light theme.
- One calm theme: neutral colors, one blue accent, no gradients or glows. Colors only from `Theme::`.
- Button or label changed → update [docs/USER-GUIDE.md](docs/USER-GUIDE.md).
- Website text lives in `frontend/src/i18n/{en,fr,es,ar}.ts`; add every key to all four. Only claim what the app
  really does: no profit promises.

## Commits and pull requests

- Branch from `main`, one topic per pull request.
- Commit subject: a short plain sentence in the imperative, no prefix
  (`Read "Stop: 151.90" as the stop loss`, `Dashboard: one-line Get started`).
- User-visible change → one line under `## Unreleased` in [CHANGELOG.md](CHANGELOG.md), written for users.
- Don't add AI co-author or "generated by" lines.
- The pull request says what changed, why, how it was tested, and which danger zones it touches.

## When to stop and ask

Stop and ask the owner instead of guessing when:
- the task would break a hard rule or needs a danger-zone change it didn't mention;
- you'd need the private backend, a signing key, a real account or a real MetaTrader;
- a test fails and you don't know why. Never delete, skip or loosen a test to get green;
- the request is unclear about trading behavior (sizes, prices, when to enter or close).

## Before you say "done"

- [ ] Release x64 builds with no new warnings
- [ ] `x64\Release\Pipkite.Tests.exe` ends with `0 failed`, and the e2e test ran (not skipped)
- [ ] New behavior has a test; every new signal layout is a case in `FormatCorpusTests.cpp`
- [ ] `git diff` shows only what the task needs: no reformatting, no generated files edited by hand
- [ ] Staged by name; no secrets, `.env`, `x64/`, databases or private folders
- [ ] Tooltips, USER-GUIDE, website text (4 languages) and CHANGELOG updated where users notice the change

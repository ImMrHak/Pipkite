# Development

## New here?

1. Install Visual Studio 2026 with the parts listed below.
2. Open `Pipkite.sln`, pick **Release x64**, build, and run `x64\Release\Pipkite.Tests.exe`. Everything should pass.
3. Read [AGENTS.md](../AGENTS.md): the project's rules and the mistakes that break things.
4. Try the app safely with the test terminal (below). Never test on a real trading account.
5. Make one small change, add a test, run the tests again, and open a pull request.

Most common change: a channel's signals aren't read right. Add the message to
`Pipkite.Tests/FormatCorpusTests.cpp` with what it should read, then fix the format (see "How it fits together").

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
The build signs the sample parser and the bridge with a local dev key (`%LOCALAPPDATA%\PipkitePublisher\devkeys`).
Installed apps only accept parsers signed with it, so back that folder up. It is kept out of the app's data folder
(`%LOCALAPPDATA%\Pipkite`) because that one gets deleted to reset the app. Older public keys stay trusted through
`trusted_keys\` in the repo (e.g. `dev-publisher-2026-10-10.pub`), so modules signed before a key change still load.

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
- **How installed apps update:** each time the UI opens (`updates.checkApp`) and every 6 hours, the engine reads
  GitHub's latest release
  (`Pipkite.Engine/AppRelease.h`). A newer version shows a bar in the app; **Update now** downloads the
  `Pipkite-<version>-setup.exe` asset, and runs it silently only if it's signed with the same certificate as the
  running app (`Updater::downloadAppUpdate`). So every GitHub release must have that exact asset name, and the
  installer must come from `package.ps1` (it signs it). Drafts and pre-releases are never offered.
- **Website:** see `frontend/` (`npm install`, `npm run dev`).

## How it fits together

```
Telegram (TDLib) -> Engine -> parser -> rules/planner -> journal (SQLite) -> named pipe -> MT5BridgeHost.dll -> EA -> broker
                       \-> backend (license, AI review, parser updates)
UI (WinForms) <-> Engine over a local named pipe
```

- The engine is native C++ and does all the work; the UI only displays and sends commands.
- Every order is written to the journal before it's sent, so a crash never sends twice.
- Parsers are either signed modules in `plugins\<id>\<version>\` or JSON formats (`ParserTemplate`) run by the
  core parser. Both are verified and hot-reloaded. The shipped formats are `builtinTemplates()` (exported to
  `config/templates/` by `Pipkite.Tests.exe --export-samples <repo>`); a channel uses a format only after the user
  picked it in **Detect format**. `emojiLabels` maps an emoji to a label word (🎯 -> TP).
- `Pipkite.Tests/FormatCorpusTests.cpp` holds real-world layouts with what each format must read, and results and
  chatter no format may read as a signal. Add a case there for every new layout. Taught formats are saved as `channel_<chatId>`, one version each time.
- The backend delivers a format as a `parser_template` component: a folder with `manifest.json`
  (`{"type": "parser_template", "version": "1.2.0"}`), `template.json` (same id and version) and optionally
  `fixtures.json`. Put it in `_shared/<id>/` to offer it to everyone, or in a license folder. It has to pass its
  fixtures and the shared negative corpus before it activates. Serving an older version rolls clients back.
- `Pipkite.Plugin.GoldParser` is a sample parser module, handy for tests.
- **Waiting for the user's OK.** Each channel keeps the parser `id@version` the user confirmed (`confirmedFormat` on
  `SourceBinding`). While its parser is another one (a new channel, a new format, a new delivered or built-in
  version), `Engine::evaluate` turns trades into Review with "New format for this channel". **Start copying** on
  Signal rules (`source.confirmFormat`) records the OK. Only the engine sets it: `savePolicy` ignores what the UI
  sends. Channels from before this got their current format as confirmed on first start. So bump a built-in
  format's `version` whenever you change it, or channels keep trading with changed rules without being asked.
- **Shared formats.** Detect format asks the backend for the formats other users made for the same channel
  (`GET /formats/channels/<chatId>`, sends the chat ID; setting `formats.lookup`). Each must pass its own examples
  and the negative corpus in the app before it's shown; its SL/TP rules are forced on. Using one adopts it as the
  channel's own `channel_<chatId>` format. With **Offer this format...** (`formats.share`), `parser.adopt` posts the
  format and its redacted examples (`POST /formats/channels/<chatId>`).
- **AI format help.** `ai.suggestFormat` sends redacted recent messages to Pipkite's backend (`POST /ai/format`) or
  to the user's own OpenAI-compatible provider (`ai.formatProvider` = `own`, key in the DPAPI secret `ai_own_key`).
  The AI returns words to add, never a reading; `applyFormatSuggestion` (`Pipkite.Core/FormatAssist.cpp`) accepts
  only word lists, symbol aliases, emoji labels and the number format, and never the safety rules. The prompt is in
  `formatSuggestionPrompt()` and, identical, in the backend's `ai/format-prompt.md`: change both.

## Rules for changes

- Run the tests before a pull request.
- Never commit keys, `*.key`, `var/`, or local data.
- Keep the UI text plain and short.

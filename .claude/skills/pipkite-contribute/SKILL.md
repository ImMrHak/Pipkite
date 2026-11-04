---
name: pipkite-contribute
description: Step-by-step recipes for changing Pipkite (Telegram to MetaTrader 5 signal copier, C++/C++CLI on Windows). Use for any code change in this repo - adding or fixing a signal format or parser case, adding an engine method, a setting or an execution-policy option, changing the database, the EA/bridge or the WinForms UI, editing website text, writing tests, or preparing a pull request.
---

# Contributing to Pipkite

`AGENTS.md` at the repo root has the hard rules and the danger zones. Read it first; this file is the how-to.
Every recipe names a real example in the code. Open it and copy its shape.

## Step 0, every time

1. `git status` and `git log --oneline -5`. Note the untracked files: they are not yours, never stage them.
2. Read the code you will change and its callers before editing (`grep` the function name across the repo).
3. Find the closest existing example (named in each recipe below).
4. Write the test first when you can, and see it fail.
5. Make the smallest change, then build and run **all** tests:

```powershell
MSBuild Pipkite.sln -p:Configuration=Release -p:Platform=x64 -m
x64\Release\Pipkite.Tests.exe            # all; last line must say "0 failed"
x64\Release\Pipkite.Tests.exe <part>     # only tests whose name contains <part>
```

`MSBuild` not on PATH: it's in `C:\Program Files\Microsoft Visual Studio\18\<Edition>\MSBuild\Current\Bin\amd64\`.
`LNK1104`: close running `Pipkite*.exe` (never `terminal64.exe`).

## Writing a test

Small built-in framework (`Pipkite.Tests/TestFramework.h`). A `TEST` registers itself.

```cpp
TEST(planner_skips_signal_when_spread_is_too_wide) {  // snake_case, reads like a sentence
    ...
    CHECK(!plan.ok);
    CHECK_EQ(plan.reason, std::string("..."));
}
```

Copy the setup from a neighboring test in the same file; don't invent APIs. Use `FakeClock` for time.

| Testing | File |
|---|---|
| parser logic, templates | `ParserTests.cpp` |
| real-world signal layouts | `FormatCorpusTests.cpp` |
| rules, sizing, entry ranges, force-enter | `PlannerTests.cpp` |
| database, IPC, signing | `StorageProtocolTests.cpp` |
| finding MetaTrader installs | `TerminalLauncherTests.cpp` |
| engine + bridge + FakeTerminal together | `EndToEndTests.cpp` (name contains `e2e`) |

A new test `.cpp` must be added to `Pipkite.Tests.vcxproj` and `.vcxproj.filters` (CMake picks it up by itself).
Same for any new `.cpp`/`.h` in other projects: add it to that project's `.vcxproj` and `.filters`.

Never delete, comment out or weaken an existing test to get green. If one fails, your change broke something.

## Recipe: a signal layout isn't read, or is read wrong

The most common contribution. Example: `cases()` in `Pipkite.Tests/FormatCorpusTests.cpp`.

1. Add the message with what it **must** read:
   `{"universal", "<text>", "BUY", "XAUUSD", entryLow, entryHigh, sl, {tp1, tp2}, "LIMIT"}`
   Entry `0, 0` = market; last field only for pending orders (`"LIMIT"`/`"STOP"`).
   Results, chatter and management ("TP1 hit +40 pips", "move SL to BE", "close half") go in with
   `side = nullptr`: no format may read them as a signal. Add one of these too when your fix widens a format.
2. `x64\Release\Pipkite.Tests.exe shipped_formats`: watch it fail.
3. Fix with the smallest change, preferring in this order:
   1. a word list or alias in `universalTemplate()` (`Pipkite.Core/ParserTemplate.cpp`): `buyWords`, `slLabels`,
      `tpLabels`, `symbolAliases`, `emojiLabels`, `commentaryWords`, `managementPhrases`, ...
   2. a language preset (`applyArabicPreset`, `applyEuropeanPreset`, ...),
   3. only then the parser itself (`Pipkite.Core/SignalParser.cpp`, a danger zone).
4. Run **all** tests. One layout fixed and another broken is not a fix.
5. Never default a missing SL/TP/entry/symbol/side. Never relax `requireStopLoss`/`requireTakeProfit` in a
   shipped format. Watch for traps: `STOP` is both an order kind and an SL label, `20x` is leverage not a price,
   `1.234,5` vs `1,234.5`, TPs marked `OPEN`.
6. Regenerate the generated files and commit them with the change (never edit them by hand):
   `x64\Release\Pipkite.Tests.exe --export-samples .`
   This rewrites `config/templates/*.json` and `Pipkite.Tests/fixtures/spec_fixtures.json`. Check `git diff` on
   them: only the formats you changed should differ.

### New shipped format

Only when a layout can't fit an existing format without breaking others. Add a block to `builtinTemplates()` in
`ParserTemplate.cpp`, copying `universal_emoji`: start from `universalTemplate()`, set a new unique `id` (never
reuse or rename an existing id: users' channels point at it), a short `name`, a one-sentence `description`. Add
corpus cases naming the new id, run `--export-samples`, add a CHANGELOG line.

## Recipe: anything that changes how a channel is read

Built-in format, delivered format, shared format, taught format: any new `id@version` makes the channel wait for
the user's OK (Start copying) before it trades. Keep it that way:
- Changed `universalTemplate()` or another built-in? Bump its `version`, or channels keep trading with the new
  rules without being asked.
- Never set `SourceBinding::confirmedFormat` outside `source.confirmFormat`; `savePolicy` keeps the engine's value.
- Text from an AI or another user only becomes rules through `applyFormatSuggestion` (AI) or the shared-format
  checks in `source.detectFormat` (examples + negative corpus pass, SL/TP forced on).
- Changed the AI prompt? Change `formatSuggestionPrompt()` and the backend's `ai/format-prompt.md` together.
- Tests: `FormatAssistTests.cpp`, the e2e tests `e2e_detect_teach_and_adopt_a_channel_format` and
  `e2e_ai_format_help_with_the_users_own_key` (a stand-in AI provider on 127.0.0.1, never a real one).

## Recipe: new engine method (the UI asks the engine for something)

Methods live in `Pipkite.Engine/EngineUi.cpp`, named `area.verb` (`parser.learn`, `mt5.launch`). Pick the kind:

**Fast** (only engine state, no waiting). Example: `source.testParse`.
Add `if (method == "area.verb") { ... }` in `Engine::handleOnEngineThread`. It runs on the engine thread, so it
must not wait on network, disk scans, Telegram or long loops. That would freeze trading.

**Slow** (network, disk, Telegram, anything that can wait). Example: `source.detectFormat`, `license.validate`.
1. Add the name to the `slow` set (`static const std::set<std::string> slow{...}` in `EngineUi.cpp`) so it runs on
   its own thread.
2. Put the method in `Engine::handleUiRequest`, **not** in `handleOnEngineThread`.
3. Do the slow work there, outside the engine thread. Touch engine state (`policy_`, `parsers_`, `store_`, ...)
   only inside `callSync([&] { ... })`, and keep that block short. Never hold it across a network call.

Both kinds:
- Read params with defaults (`params.value("limit", 3)`, `jsonInt64(...)` for chat ids) and clamp numbers. The
  UI is a trust boundary too.
- Errors: `fail("Plain sentence the user can read")`.
- Replies must fit a 64 KiB frame: cap lists and string lengths (see the limits in `source.detectFormat`).
- Read-only methods say so in a comment and record nothing.
- Test-only methods check `PIPKITE_TEST_HOOKS=1` like `test.injectMessage` and do nothing otherwise.

UI side: `client_->Call("area.verb", params, gcnew ResponseHandler(this, &MyPage::OnDone));` with
`void OnDone(bool ok, Object^ result, String^ error)`. Callbacks already run on the UI thread. Always handle
`!ok` and show `error`. Read the result with `J::Obj`, `J::Str`, `J::Num`, `J::Bool`, `J::Arr`
(`Pipkite.WinForms/Json.h`). See `Wizard.h` `OnTested`.

## Recipe: new app setting (UI preference stored as text)

Example: `ui.setupHidden`.
1. Add the key to `kUiSettings` at the top of `EngineUi.cpp` (unknown keys are rejected on purpose).
2. UI: read it from the `settings.get` reply; save with `settings.save` and params
   `{ "values": { "key": "value" } }` (see `SettingsPage.h`). Values are strings; booleans are `"true"`/`"false"`.
   Always handle a missing value (older installs don't have it).
3. If the engine must react, refresh its cached copy where `settings.save` updates `keepAwakeSetting_`.
Never store secrets in plain settings without the owner's OK.

## Recipe: new execution-policy option (changes trading)

Example: `forceEnterMissed` / `forceEnterPips`. Every step is required:
1. Field in `ExecutionPolicy` (`Pipkite.Core/Policy.h`) with a default that keeps today's behavior. New risky
   behavior is **off** by default.
2. `Policy.cpp`: add it to `toJson`, to `fromJson` with the **same** default (old saved policies and backups don't
   have it), and a range check in `validate()` with a plain message.
3. Use it in `Planner.cpp` (or where the decision is made).
4. `PolicyPage.h`: a control with a tooltip via `L::AddField`, loaded with `J::Num/J::Bool(policy_, ..., default)`
   and written back in the save function.
5. `PlannerTests.cpp`: a test with it off (unchanged behavior) and one with it on, plus one at the limits.
6. USER-GUIDE and CHANGELOG.

## Recipe: database change

`Pipkite.Storage/Schema.cpp`, a danger zone: users' databases already exist.
- **Never edit `kMigration1`** or any existing migration. Installed databases already ran it and won't again.
- Add a new migration: bump `kSchemaVersion` in `Schema.h`, add `kMigration2` with the change, and in `migrate()`
  apply it when `current < 2`, inside a `Transaction`, recording the version in `migrations` like version 1.
- Only additive changes: new tables, new nullable or defaulted columns. Never drop or rename columns with user data.
- Backups check `kSchemaVersion` (`Backup.cpp`). Test that a v1 database migrates and its data survives
  (`StorageProtocolTests.cpp`).
- Ask the owner before any schema change.

## Recipe: EA / bridge / plugin ABI

A danger zone: users keep their old EA running until they restart MetaTrader, and installed parser modules stay.
- Messages between EA, bridge and engine: add fields, never remove or rename them; tolerate missing fields from
  an older EA. Change `MT5BridgeImpl`, `mql5/PipkiteEA/PipkiteEA.mq5` and `Pipkite.FakeTerminal` together, and
  extend the e2e test.
- `PipkiteEA.mq5` and `PipkiteEA.ex5` are both in git and must match. Recompile with
  `powershell -File mql5\compile-ea.ps1 -OutDir x64\Release\mql5` (needs MetaTrader 5 installed) and commit both.
  Can't compile? Say so in the PR; don't commit a `.mq5` change alone.
- Test EAs only on a **demo** account, only with the owner's permission. Never attach to their live terminal.
- Don't change `include/PipkitePluginAbi.h` structs or `SR_PLUGIN_ABI_VERSION`/`SR_BRIDGE_IMPL_ABI_VERSION`
  without the maintainers: every installed module would stop loading.

## Recipe: UI change

- Pages are header-only C++/CLI files in `Pipkite.WinForms/` (`DashboardPage.h`, `PolicyPage.h`, ...). Helpers:
  `Controls.h` (`L::Btn`, `L::Tip`, `L::AddField`, `L::Num`, `L::OnAnyChange`), colors in `Theme.h`
  (`Theme::Accent`, `Success`, `Warning`, `Danger`, `Text`, `TextMuted`, ...).
- Lay out with `FlowLayoutPanel`/`TableLayoutPanel` like the surrounding page; no absolute `Point` positions
  (they overlap at other sizes and scales).
- C++/CLI traps: no chained assignment (`a->X = b->X = 1`); inside a Form write
  `System::Windows::Forms::DialogResult` in full; unsubscribe from engine events when a form closes.
- Check it on an isolated instance with FakeTerminal (commands in `AGENTS.md`) at 1024x640, 150% scaling, dark and
  light theme.
- Changed a label or button: update `docs/USER-GUIDE.md` and its screenshot in `docs/images/` if shown.

## Recipe: website change (`frontend/`)

- `cd frontend; npm install; npm run dev` (http://localhost:4321). `npm run build` before you push: it type-checks,
  and a key missing from one language fails it (`fr`, `es`, `ar` are typed as `Dict` from `en.ts`).
- Text: `frontend/src/i18n/en.ts`, `fr.ts`, `es.ts`, `ar.ts`. Write real translations, not English copies. Arabic
  is right-to-left; check `/ar/`.
- No trackers, external scripts or fonts. No claims the app can't back up (profits, "guaranteed", made-up numbers).
- `frontend/src/config.ts` versions and downloads are set at release time by the maintainers; don't change them.

## Things you can't do from this repo

- The backend is private. Don't invent endpoints or response shapes. Open an issue describing what you need.
- Releases, version numbers, signing and `trusted_keys/` belong to the maintainers.

## Pull request checklist

- [ ] Release x64 builds with no new warnings; `Pipkite.Tests.exe` ends with `0 failed`; e2e ran, not skipped
- [ ] A test that fails without the change; a corpus case for every new layout, plus a negative case if a format
      was widened
- [ ] Generated files regenerated with `--export-samples`, not hand-edited
- [ ] New files added to the right `.vcxproj` and `.filters`
- [ ] `git diff` has only the task: no reformatting, renames or encoding changes
- [ ] Files staged by name; no secrets, `.env`, `x64/`, databases or private folders
- [ ] Danger zones touched are named in the PR, with tests for old and new behavior
- [ ] Tooltips, USER-GUIDE, website text (4 languages), CHANGELOG `## Unreleased` updated where users notice
- [ ] Commit subject is a short plain sentence; no AI co-author lines

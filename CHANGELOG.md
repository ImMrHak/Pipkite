# Changelog

## 1.0.7

- **After updating, press Start copying once per channel.** The built-in formats now read a take profit inside the
  entry zone (see below), so a channel that uses one shows **Not copying yet** on Signal rules. Select it there to see
  its latest signals as Pipkite reads them. If they're right, press **Start copying**. Its settings are kept.
- **No more "Market is closed" after the market reopens.** When MetaTrader connected while the market was closed (a
  weekend, the daily break, a holiday), Pipkite kept refusing every signal as "Market is closed" until MetaTrader
  reconnected, even with the market trading. It now sees the market open again as soon as prices move.
- **A take profit inside the entry zone is used.** "BUY 3620-3626, TP 3625, TP 3630" was refused as invalid. Pipkite
  now compares the targets with the price it really buys at: bought at 3622, TP1 3625 is used; bought at 3625.50, TP1
  is left out and the next target is used. With no target beyond the price, nothing is traded. History shows which
  targets were left out, and the AI review counts the same way.

## 1.0.6

- **After updating, press Start copying once per channel.** The built-in formats now read a few messages differently
  (see below), so a channel that uses one shows **Not copying yet** on Signal rules. Select it there to see its
  latest signals as Pipkite reads them. If they're right, press **Start copying**. Its settings are kept.
- **Setting up a channel is one click.** Tick a channel, or select it on Signal rules: Pipkite reads its latest
  messages with every format and shows, in plain words, how many signals it read and the latest ones as it reads
  them. If they're right, press **Start copying**. **Not right?** opens the format window to pick another format or
  teach one. Detect format, custom parser requests, parser updates and the installed parsers are under **Show
  advanced tools**. With **Show formats other Pipkite users made for the same channel** on (Settings), checking a
  channel sends its ID to find them, as Detect format did.
- **A trade needs a real setup:** an entry price, a stop loss and a take profit. BUY NOW or SELL NOW without an entry
  price goes to Review and is never traded, whatever the channel's settings. Only the stop loss can be left out, and
  only with the option below.
- **Max stop distance per symbol.** In Symbol limits, set how far the stop loss may be from the entry (for gold,
  20.00 is suggested). If a message's stop is further, Pipkite uses the closer one. A published stop is never
  widened, and if the broker won't accept the stop, the trade is skipped.
- **Signals without a stop loss (off by default).** A new option in Symbol limits trades them with the symbol's max
  stop distance, only when everything else is clear and the message doesn't say the stop is still to come
  ("SL: soon"). History shows where the stop came from.
- **Numbers that aren't prices are never traded as prices.** A stop loss written in pips ("SL 30 pips"), a stop or
  target less than half or more than double the entry price ("SL 30" for gold at 4000), and a take profit without
  a price ("TP⁴ #Open", "TP3 (open)") are no longer read as prices. Before, such a signal could be traded with a stop
  of 30 or a take profit of 4. Now a stop or target like that sends the signal to Review, and an open TP is simply
  left out.
- **More real signals are read.** "BUY NOW FROM 4040/4037" (the entry after FROM), signals written in styled
  letters (𝐗𝐀𝐔𝐔𝐒𝐃, 𝖲𝖫 𝟒𝟎𝟑𝟒), and signals ending with a tip like "after 40 pips set breakeven". Result posts that
  name the trade ("XAUUSD SELL · TP 3 HIT 90 pips"), "Hold BUY" posts and @usernames like @buy_signals are no longer
  read as signals waiting for review.
- **Sell signals with a "long term" footer are read again.** A Buy or Sell word in running text, on a line with
  no price and no symbol (like "Money management is key to long term success"), no longer counts as a direction.
  Such Sell signals went to Review as "Both directions found".
- **Channel settings.** On Signal rules, each channel can have its own broker symbol, way to enter (wait for the
  price, right away, or pending orders) and its own sizing or take-profit. These settings existed but could not be
  changed. Signal rules shows them for every channel.
- **Broker symbol names just work.** Symbols like XAUUSD.m, GOLD#, US30.cash or DJ30 in Symbol limits now receive the
  matching signals, also from channels that post several symbols. Before, gold signals written "GOLD" only went to a
  symbol named exactly XAUUSD, and indices and oil could not be set up at all.
- **A channel pinned to one symbol never trades another.** A message for a different instrument is skipped with a
  clear reason. The pin can now be removed again (each message's own symbol).
- **Unticking a chat pauses it** and keeps its format, OK to copy, symbol and sizing. Before, it was deleted, and a
  quick tick right after opening the page could remove every other channel.
- **New symbols get prices at once.** A symbol added to Symbol limits while MetaTrader is connected is streamed right
  away; before, it waited for the next reconnect and its signals were rejected.
- **Trading hours can run overnight**, e.g. 22:00 to 06:00. A mistyped time now shows an error instead of being
  replaced.
- **Close Pipkite positions / Cancel Pipkite pending orders** buttons on the MetaTrader page. They only touch what
  Pipkite placed, after you type the account number.
- **No more sideways scrolling** on a small window: tables, text and button rows fit the window, also after you
  shrink it.
- **Settings show only what applies.** Execution policy shows lots or risk % depending on the sizing mode, the
  split percentages only for split targets, and each tolerance or window only when its option is ticked. Telegram
  sign-in shows one step at a time. Your own AI key fields appear only when you choose your own AI. The update
  address and license link boxes, meant for the Pipkite team, are no longer shown.
- **Clearer status after a restart.** The live feed says no new signals came in since Pipkite started, instead of
  asking you to add a Telegram source, and the MetaTrader page no longer says "MetaTrader is starting" once it is
  connected.
- **The AI review says how likely a signal is to work.** Its score is the estimated chance that price reaches TP1
  before the stop loss, measured from where Pipkite enters: **Good** from 60%, **Bad** at 40% or less. A far TP1 pays
  more but is reached first less often, so it can be Bad. The reasons show the distances, the trend and the spread.
  Without MetaTrader prices a signal is Uncertain at best, and buy stop and sell stop orders are no longer marked Bad
  while price is still on the far side of the stop.
- **Detailed log for troubleshooting (off by default).** Tick it in Settings > Diagnostics and Pipkite writes what it
  receives and does to one file per day: each message, what it read, why it traded or not (with the price it saw),
  the orders, MetaTrader's replies, Telegram's connection and what you did in the window. Passwords, login codes and
  keys are never written. **Open log folder** shows the files to send to support. They stay on your PC, are deleted
  after the message retention days, never pass 100 MB, and go with **Erase local history**.
- **No more "Parser module rejected" at every start** for an old parser download made before the rename to Pipkite.
  It never loaded and stays unused.

## 1.0.5

- **Symbol limits are easier to set.** They are now at the top of Execution policy: pick a symbol from your
  broker's list and fill in its max spread, entry deviation and max lots. A line shows which symbols are traded.
- **Suggested limits for common symbols.** For gold, silver and the main currency pairs, Pipkite shows starting
  values. Click **Use suggested limits**, check them, then save. Nothing is traded until you save.
- **Page tours.** The first time you open a page, a short tour points at each button and explains it. Each tour
  shows once; Settings has **Show page tours again**.
- The Dashboard cards and a few Settings buttons now explain themselves when you hover over them.
- **One AI review per signal.** When a channel edits or reposts a signal without changing it, Pipkite shows the
  first AI verdict again instead of asking for a new review, so the same signal is no longer saved several times.
- **A calmer Live feed.** The Dashboard shows only signals that were accepted or are still running. Rejected, ignored
  and held signals are in History, which can now also filter by Ignored.

## 1.0.4

- **Update from inside the app.** When a new version is out, a bar at the top says so. **Update now** downloads it,
  checks it is signed by Pipkite, closes Pipkite, installs it and opens it again. Open trades are not touched.
  Portable copies get a **Download** button instead. Pipkite asks GitHub for the latest version each time it
  starts and every few hours after. The bar slides in, with a light running along its edge while it downloads.
- Removed the old, unused "staged core update" code; the launcher now just starts Pipkite.
- This version has to be installed by hand once (1.0.3 and older can't update themselves); later versions
  update from inside the app.
- Website guide: a new section explains how Pipkite reads a signal, step by step, with an example.

## 1.0.3

- New channels without a custom parser: **Detect format** shows what every format reads from your own example
  and from the channel's recent messages, before you pick one.
- Six formats ship with the app: Universal (9 languages; gold, forex, indices, crypto), Emoji labels (🎯 TP, 🛑 SL),
  Comma decimals, Thousands separators, Turkish & Indonesian, Strict labels only.
- Reads more layouts: EUR/USD and BTC/USDT, "Stop: 151.90" as the stop loss (BUY STOP stays a stop order),
  leverage like "LONG 20x", TP lists split by / or commas.
- **Teach Pipkite**: select the symbol, side, entry, SL and TPs in a message, test the result on recent messages,
  then use it. Each change is a new version you can roll back.
- Formats can be delivered and updated by the backend while Pipkite runs, without an app update.
- Formats other Pipkite users made for the same channel show up in **Detect format**, with how many use them. Tick
  **Offer this format to other Pipkite users of this channel** to share yours. Sends the channel's ID; it can be
  turned off in Settings.
- **Ask AI for a format**: the AI suggests words to add to a format, and you check the result on the channel's
  messages before using it. Uses Pipkite's AI with your license key, or your own OpenAI, DeepSeek or other
  OpenAI-compatible key (Settings > Signal formats and AI help). The AI never reads or places a trade.
- A new or changed format doesn't trade right away: its signals show in History with what Pipkite would have done,
  until you click **Start copying** on Signal rules. New versions of a format a channel uses ask again.
- A taught format is only saved if it reads your example the way you marked it. Changing the example after
  testing no longer saves the format with an untested example.
- **Teach Pipkite from this message** in a message's details, for messages a format couldn't read.
- Pending orders at a price far from the market (like 23.50 for 2350) are refused as misread, and Detect format
  marks such readings **Check price**.
- A warning when a channel's recent signals can't be read any more (it may have changed its layout).
- A format update that failed to install is no longer downloaded again every 30 minutes.
- Simpler user guide and README: a setup checklist, step-by-step setup, a troubleshooting table and a glossary.
- The download no longer includes developer notes, only the user guide.
- Unchanged: a trade is only placed when symbol, side, entry (or NOW), SL and TP are all read with no conflict.

## 1.0.2

- The AI review reads the market: MetaTrader sends the latest M1, M5, M15, H1 and H4 candles with each signal.
  It checks the trend, where price is against the entry, the SL against volatility, the spread and the recent
  H1 highs and lows. Signals whose price already passed TP1 or the SL are marked Bad. Only prices are added.
- After updating, close MetaTrader and press Start MetaTrader in Pipkite once to install the new EA.
- Dashboard: Get started takes one line, the page scrolls on small screens, the AI verdict sits next to Side.
- A requested parser arrives while Pipkite runs (checked every 30 minutes), no restart needed.

## 1.0.1

- Finds MetaTrader 5 from any broker (HFM, IC Markets, ...), including portable and never-opened installs.
- MetaTrader not listed? "Choose folder..." picks its folder and checks Pipkite can work with it; after Start,
  Pipkite confirms MetaTrader really connected.
- Setup: once a channel's parser request is sent you can continue; the parser installs itself when ready.
- New parser signing key. Install this version to receive new parsers; parsers you already have keep working.
- Pipkite logo on the desktop and Start menu shortcuts and on the installer.

## 1.0.0

First release.

- Copies Telegram signals to MetaTrader 5, about 4 ms from message to order.
- Parsers built per channel, requested from the app and delivered as signed updates (no restart).
- AI review of every signal (Good / Uncertain / Bad with reasons).
- Rules: fixed lots or risk %, spread and slippage caps, trading hours, entry ranges, force-enter, split targets.
- One-click pause / resume. Crash-safe order journal.
- Dashboard with profit, live feed and latency. Dark and light themes.
- Free license key, no limits.

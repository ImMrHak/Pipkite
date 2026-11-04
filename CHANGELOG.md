# Changelog

## Unreleased

- New channels without a custom parser: **Detect format** shows what every format reads from your own example
  and from the channel's recent messages, before you pick one.
- Six formats ship with the app: Universal (9 languages; gold, forex, indices, crypto), Emoji labels (🎯 TP, 🛑 SL),
  Comma decimals, Thousands separators, Turkish & Indonesian, Strict labels only.
- Reads more layouts: EUR/USD and BTC/USDT, "Stop: 151.90" as the stop loss (BUY STOP stays a stop order),
  leverage like "LONG 20x", TP lists split by / or commas.
- **Teach Pipkite**: select the symbol, side, entry, SL and TPs in a message, test the result on recent messages,
  then use it. Each change is a new version you can roll back.
- Formats can be delivered and updated by the backend while Pipkite runs, without an app update.
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

# Pipkite user guide

## Setup in 6 steps

The setup wizard opens on first launch and walks you through these. You can redo any of it later from the menu.

1. **License key.** Paste your free key (get one on the website) and click **Validate & save**.
2. **Telegram.** Get an API ID and hash at my.telegram.org (API development tools), enter them, then your phone
   number and the code Telegram sends you.
3. **Channels.** Tick the channels and groups you want to copy. Only new messages are copied, never old ones.
4. **MetaTrader 5.** Click **Start MetaTrader**, log in to your broker, then approve the pairing with your account
   number. Use a demo account first: once an account is paired, copying is live.
5. **Signal format.** On **Signal rules**, select each channel and click **Detect format**.
   - Paste one of the channel's signals under **Your example** and click **Check example**. Every format shows what
     it reads from it: symbol, side, entry, SL and TPs. The channel's recent messages are checked the same way below.
   - Formats other Pipkite users made for the same channel are listed too, with how many use them. They're checked
     on your own messages like any other.
   - Pick the format whose rows marked Signal are right, then click **Use this format**. Messages marked Review or
     Ignored are never traded. **Check price** means the price is far from today's market: that reading is wrong.
   - Formats that come with Pipkite: **Universal** (English, French, German, Italian, Spanish, Portuguese, Arabic,
     Persian, Russian; gold, forex, indices, crypto), **Emoji labels** (🎯 TP, 🛑 SL), **Comma decimals** (1,0850),
     **Thousands separators** (18,250), **Turkish & Indonesian** and **Strict labels only**.
   - None reads it right? Under **Teach Pipkite**, select each part of your example (symbol, Buy or Sell, entry, SL,
     each TP), press its button, then **Test on recent messages**. If the result looks right, use it. Pipkite only
     saves it if it reads your example the way you marked it.
   - Or click **Ask AI for a format**: the AI suggests words to add (from your example and the recent messages, with
     links, @names, phone numbers and emails removed), and you check the result the same way. It uses Pipkite's AI
     with your license key, or your own key (Settings). The AI never places or decides a trade.
   - Tick **Offer this format to other Pipkite users of this channel** to share what you taught or picked.
   - Still no luck? Click **Request a custom parser**. We build one and it installs by itself, no restart.
   Until a channel has a format, its messages aren't traded.
6. **Start copying.** A new format doesn't trade right away. The channel's signals show in History with what
   Pipkite would have done. When they read right, select the channel on **Signal rules** and click
   **Start copying**. The same happens when a channel's format changes or gets a new version.

Until all of this is done, the Dashboard shows a **Get started** list with what's still missing and a button
to each step, and the top bar says **Not ready**.

Before risking money, use **Test a sample** in the wizard: paste a real message and see what Pipkite reads.
Nothing is sent.

![Welcome wizard](images/welcome-wizard.png)

## Screens

**Dashboard.** Connection status, balance and profit, the live feed of signals and what happened to each, and
the measured latency.

![Dashboard](images/dashboard.png)

**Telegram sources.** Sign in, then tick the chats you want to copy.

**MetaTrader.** Start the terminal with the EA, approve pairing, and map each channel's symbol to your broker's
(for example GOLD to XAUUSD).

![MetaTrader](images/metatrader.png)

**Signal rules.** Which format reads each channel. Detect, teach or ask the AI for one, or request a custom parser.
A channel marked **Not copying yet** has a new format: check its signals in History, then click **Start copying**.

![Signal rules](images/signal-rules.png)

**Execution policy.** How trades are placed:

- size: fixed lots, or % of equity risked at the stop-loss (always rounded down)
- targets: one take-profit, or split across several
- limits: max signal age, spread, slippage, open positions, daily loss, trading hours
- entry ranges: waits for price to enter "buy 4299-4295" style ranges; **force-enter** (off by default) takes the
  trade if price slipped just past it by a few pips
- AI review: turn on "review every signal" to see Good / Uncertain / Bad next to each signal

Every symbol you trade needs a max spread and deviation, otherwise it isn't traded.

![Execution policy](images/execution-policy.png)

**History.** Every message, how it was read and what was done, with the reason. Export to CSV or JSON. Open a
message Pipkite couldn't read and click **Teach Pipkite from this message** to fix its channel's format from it.

![History](images/history.png)

**Settings.** Theme, start with Windows, data retention, backups, license key, updates. Under **Signal formats and
AI help**: whether to look up and share formats with other users of a channel, and which AI helps with formats
(Pipkite's, or your own OpenAI, DeepSeek or other OpenAI-compatible key, stored encrypted on your PC).

## Pausing

**Pause copying** (top bar or tray icon) stops new trades right away. It never closes anything: manage open
trades in MetaTrader as usual.

Sleep, shutdown or a closed MT5 also pause copying. For 24/7, run it on a Windows VPS.

## AI review

Each signal gets a verdict (Good, Uncertain or Bad) with reasons and a confidence score. It's advice: it doesn't
block trades. It's an early version, it can be wrong, and the score is not a promise of profit.
With MetaTrader connected, the review also reads the market: the trend on H4, H1 and M15, momentum, where price is
now against the entry, SL and TP1, and the SL distance against volatility.
Only the signal fields (symbol, side, entry, SL, TPs) and recent price candles of that symbol (M1 to H4) are sent,
never your account or messages. The signal and its
verdict are kept with your license key to help the AI learn.

## Your data

History, settings and your Telegram session (encrypted with Windows DPAPI) stay on your PC. Only what the online
services need is sent: see the privacy page on the website for the full list. Your broker
password stays in MetaTrader, Pipkite never asks for it.

## Something wrong?

- **Telegram: not set up**: enter your API ID/hash and sign in.
- **MT5 offline**: MetaTrader is closed or the EA isn't on a chart. Click Start MetaTrader (close MT5 first if it's
  already open).
- **Signal shows Unsupported**: that channel has no format yet. Detect or teach one on Signal rules.
- **Signal shows Rejected or Review**: open the row in History to see why. Nothing was sent.
- **"New format for this channel, not copied yet"**: the channel's format is new or changed. If the reading is
  right, click **Start copying** on Signal rules.
- **"Couldn't fully read 3 of its last 4 signals"**: the channel may have changed how it writes signals. Run
  **Detect format** again.
- **Pairing never shows up**: start MetaTrader and log in to your broker first.
- **SmartScreen warning on install**: More info, Run anyway.

Settings has a **Run health check** button that checks everything at once.

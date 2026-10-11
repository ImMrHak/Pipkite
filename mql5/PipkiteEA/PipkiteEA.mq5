//+------------------------------------------------------------------+
//| PipkiteEA.mq5 - Pipkite 1.0.0 copier Expert Advisor       |
//|                                                                  |
//| Owns: authoritative account identity, fresh quotes, final broker |
//| validation, order submission and transaction reporting.          |
//| The EA never makes signal decisions: it executes validated,      |
//| journaled commands from the local Pipkite engine and refuses |
//| anything that fails revalidation.                                |
//|                                                                  |
//| Requirements: attach to exactly ONE chart per terminal/account,  |
//| enable Algo Trading and "Allow DLL imports" (MT5BridgeHost.dll). |
//+------------------------------------------------------------------+
#property copyright   "Pipkite"
#property version     "1.00"
#property description "Pipkite 1.0.0 - executes commands from the local Pipkite engine."
#property description "Requires 'Allow DLL imports' for MT5BridgeHost.dll. Attach to ONE chart only."

#import "MT5BridgeHost.dll"
int  BridgeVersion();
int  BridgeInitialize(string terminalId, long login, string server);
int  BridgeTryReceive(uchar &buf[], int capacity);
int  BridgeTrySend(uchar &buf[], int len);
int  BridgeConnectionStatus();
int  BridgeStatusText(string &buf, int capacity);
int  BridgeJournalAppend(string commandId, string state, string detail);
int  BridgeJournalState(string commandId, string &buf, int capacity);
long BridgeNowMicros();
void BridgeShutdown();
#import

input int    InpTimerMs        = 10;    // Requested timer (ms); MT5 delivers ~10-16 ms and may coalesce
input int    InpMaxBatch       = 16;    // Max queued messages processed per timer event
input int    InpQuoteMs        = 20;    // Quote publishing interval (ms); only changed quotes are sent
input int    InpMaxTickAgeSec  = 10;    // Reject when the last tick is older than this
input string InpExtraSymbols   = "";    // Extra symbols to stream (comma separated)

#define COPIER_MAGIC 20260930
#define MAX_EVENTS   256

//--- state
string   g_terminalId = "";
bool     g_paused     = false;
bool     g_ready      = false;
uchar    g_rx[];
string   g_symbols[];
double   g_lastBid[], g_lastAsk[];   // last published quote per symbol (changed-only publishing)
string   g_outbox[];               // messages waiting for outbound queue capacity
ulong    g_lastQuoteMs = 0, g_lastHeartbeatMs = 0, g_lastAccountMs = 0, g_lastStatusMs = 0;
string   g_ownerKey = "";

// request/order -> command attribution
ulong    g_reqIds[];   string g_reqCmd[];
ulong    g_orders[];   string g_orderCmd[];  double g_orderVol[];  double g_orderFilled[];
string   g_cmdPrefix[]; string g_cmdFull[];

// transaction events captured in OnTradeTransaction, processed in OnTimer
int      g_evType[MAX_EVENTS];
ulong    g_evA[MAX_EVENTS];
ulong    g_evB[MAX_EVENTS];
int      g_evC[MAX_EVENTS];
int      g_evHead = 0, g_evTail = 0;

//+------------------------------------------------------------------+
ulong NowMs() { return (ulong)TimeGMT() * 1000; }

string Fnv1a(string s)
{
   uint h = 2166136261;
   uchar b[];
   int n = StringToCharArray(s, b, 0, WHOLE_ARRAY, CP_UTF8);
   for(int i = 0; i < n; i++) { h ^= b[i]; h *= 16777619; }
   return StringFormat("%08X", h);
}

string Esc(string v)
{
   StringReplace(v, "\\", "\\\\");
   StringReplace(v, "\n", "\\n");
   StringReplace(v, "\r", "\\r");
   return v;
}

string D(double v, int digits) { return DoubleToString(v, digits); }

// line-format message builder
class Msg
{
public:
   string text;
   Msg(string type) { text = "type=" + type + "\n"; }
   void S(string k, string v) { text += "payload." + k + "=" + Esc(v) + "\n"; }
   void N(string k, double v, int digits) { text += "payload." + k + "=" + D(v, digits) + "\n"; }
   void I(string k, long v) { text += "payload." + k + "=" + IntegerToString(v) + "\n"; }
   void B(string k, bool v) { text += "payload." + k + "=" + (v ? "true" : "false") + "\n"; }
};

bool SendRaw(string text)
{
   uchar arr[];
   int n = StringToCharArray(text, arr, 0, WHOLE_ARRAY, CP_UTF8) - 1;   // drop terminating zero
   if(n <= 0) return false;
   int r = BridgeTrySend(arr, n);
   return r == 1;
}

void Send(Msg &m)
{
   // Trade events are never dropped: if the outbound queue is full they wait in the outbox.
   if(ArraySize(g_outbox) > 0 || !SendRaw(m.text))
   {
      int n = ArraySize(g_outbox);
      if(n < 4096) { ArrayResize(g_outbox, n + 1); g_outbox[n] = m.text; }
   }
}

void FlushOutbox()
{
   int sent = 0;
   while(sent < ArraySize(g_outbox) && sent < 64)
   {
      if(!SendRaw(g_outbox[sent])) break;
      sent++;
   }
   if(sent > 0) ArrayRemove(g_outbox, 0, sent);
}

// line-format parser
string g_keys[], g_vals[];

void Parse(string text)
{
   string lines[];
   int n = StringSplit(text, '\n', lines);
   ArrayResize(g_keys, 0);
   ArrayResize(g_vals, 0);
   for(int i = 0; i < n; i++)
   {
      int eq = StringFind(lines[i], "=");
      if(eq <= 0) continue;
      int k = ArraySize(g_keys);
      ArrayResize(g_keys, k + 1);
      ArrayResize(g_vals, k + 1);
      g_keys[k] = StringSubstr(lines[i], 0, eq);
      string v = StringSubstr(lines[i], eq + 1);
      StringReplace(v, "\\n", "\n");
      StringReplace(v, "\\r", "\r");
      StringReplace(v, "\\\\", "\\");
      g_vals[k] = v;
   }
}

string Get(string key)
{
   for(int i = 0; i < ArraySize(g_keys); i++)
      if(g_keys[i] == key) return g_vals[i];
   return "";
}
double GetD(string key) { return StringToDouble(Get(key)); }
long   GetL(string key) { return StringToInteger(Get(key)); }

// attribution maps
void MapRequest(ulong req, string cmd)
{
   int n = ArraySize(g_reqIds);
   ArrayResize(g_reqIds, n + 1); ArrayResize(g_reqCmd, n + 1);
   g_reqIds[n] = req; g_reqCmd[n] = cmd;
}
string CmdForRequest(ulong req)
{
   for(int i = ArraySize(g_reqIds) - 1; i >= 0; i--) if(g_reqIds[i] == req) return g_reqCmd[i];
   return "";
}
void AddOrderSlot(string cmd, double vol)
{
   int n = ArraySize(g_orders);
   ArrayResize(g_orders, n + 1); ArrayResize(g_orderCmd, n + 1); ArrayResize(g_orderVol, n + 1); ArrayResize(g_orderFilled, n + 1);
   g_orders[n] = 0; g_orderCmd[n] = cmd; g_orderVol[n] = vol; g_orderFilled[n] = 0;
}
int OrderIndex(ulong order)
{
   for(int i = ArraySize(g_orders) - 1; i >= 0; i--) if(g_orders[i] == order) return i;
   return -1;
}
void RememberCommand(string cmd)
{
   int n = ArraySize(g_cmdPrefix);
   ArrayResize(g_cmdPrefix, n + 1); ArrayResize(g_cmdFull, n + 1);
   g_cmdPrefix[n] = StringSubstr(cmd, 0, 8); g_cmdFull[n] = cmd;
}
string CmdForComment(string comment)
{
   if(StringFind(comment, "SR ") != 0) return "";
   string p = StringSubstr(comment, 3, 8);
   for(int i = ArraySize(g_cmdPrefix) - 1; i >= 0; i--) if(g_cmdPrefix[i] == p) return g_cmdFull[i];
   return "";
}

string JournalState(string cmd)
{
   string buf;
   StringInit(buf, 64, 0);
   int n = BridgeJournalState(cmd, buf, 64);
   if(n <= 0) return "";
   return StringSubstr(buf, 0, n);
}

// status helpers
void Status(string cmd, string state, string reason, double filled = 0, ulong ticket = 0, long sendUs = 0, ulong requestId = 0)
{
   Msg m("COMMAND_STATUS");
   m.S("commandId", cmd);
   m.S("state", state);
   m.S("reason", reason);
   m.N("filledVolume", filled, 2);
   if(ticket > 0) m.S("orderTicket", IntegerToString((long)ticket));
   if(requestId > 0) m.S("requestId", IntegerToString((long)requestId));
   if(sendUs > 0) m.I("sendUs", sendUs);
   Send(m);
}

void Reject(string cmd, string reason)
{
   BridgeJournalAppend(cmd, "Rejected", reason);
   Status(cmd, "Rejected", reason);
}

double NormalizeToTick(string sym, double price)
{
   double tick = SymbolInfoDouble(sym, SYMBOL_TRADE_TICK_SIZE);
   int digits = (int)SymbolInfoInteger(sym, SYMBOL_DIGITS);
   if(tick <= 0) return NormalizeDouble(price, digits);
   return NormalizeDouble(MathRound(price / tick) * tick, digits);
}

double FloorToStep(double v, double step)
{
   if(step <= 0) return v;
   return NormalizeDouble(MathFloor(v / step + 1e-9) * step, 8);
}

ENUM_ORDER_TYPE_FILLING PickFilling(string sym)
{
   long modes = SymbolInfoInteger(sym, SYMBOL_FILLING_MODE);
   if((modes & SYMBOL_FILLING_FOK) != 0) return ORDER_FILLING_FOK;
   if((modes & SYMBOL_FILLING_IOC) != 0) return ORDER_FILLING_IOC;
   return ORDER_FILLING_RETURN;
}

bool TradingAllowed()
{
   return TerminalInfoInteger(TERMINAL_TRADE_ALLOWED) && MQLInfoInteger(MQL_TRADE_ALLOWED) && AccountInfoInteger(ACCOUNT_TRADE_EXPERT) && AccountInfoInteger(ACCOUNT_TRADE_ALLOWED);
}

void Subscribe(string sym)
{
   if(sym == "") return;
   for(int i = 0; i < ArraySize(g_symbols); i++) if(g_symbols[i] == sym) return;
   if(!SymbolSelect(sym, true)) return;
   int n = ArraySize(g_symbols);
   ArrayResize(g_symbols, n + 1);
   ArrayResize(g_lastBid, n + 1);
   ArrayResize(g_lastAsk, n + 1);
   g_symbols[n] = sym;
   g_lastBid[n] = 0;
   g_lastAsk[n] = 0;
}

// snapshots
void SendAccount()
{
   int positions = 0, pendings = 0;
   double lots = 0;
   string copierSyms = "", allSyms = "";
   Msg m("ACCOUNT_SNAPSHOT");
   int ci = 0, ai = 0;
   for(int i = PositionsTotal() - 1; i >= 0; i--)
   {
      ulong t = PositionGetTicket(i);
      if(t == 0) continue;
      string sym = PositionGetString(POSITION_SYMBOL);
      m.S("netExposureSymbols." + IntegerToString(ai++), sym);
      if(PositionGetInteger(POSITION_MAGIC) == COPIER_MAGIC)
      {
         positions++;
         lots += PositionGetDouble(POSITION_VOLUME);
         m.S("copierSymbols." + IntegerToString(ci++), sym);
      }
   }
   for(int i = OrdersTotal() - 1; i >= 0; i--)
   {
      ulong t = OrderGetTicket(i);
      if(t != 0 && OrderGetInteger(ORDER_MAGIC) == COPIER_MAGIC) pendings++;
   }
   // Copier realized net today (profit + commission + swap + fee of closing deals).
   double realized = 0;
   datetime dayStart = (datetime)(TimeCurrent() - (TimeCurrent() % 86400));
   if(HistorySelect(dayStart, TimeCurrent() + 60))
   {
      for(int i = HistoryDealsTotal() - 1; i >= 0; i--)
      {
         ulong d = HistoryDealGetTicket(i);
         if(d == 0 || HistoryDealGetInteger(d, DEAL_MAGIC) != COPIER_MAGIC) continue;
         realized += HistoryDealGetDouble(d, DEAL_PROFIT) + HistoryDealGetDouble(d, DEAL_COMMISSION) + HistoryDealGetDouble(d, DEAL_SWAP) + HistoryDealGetDouble(d, DEAL_FEE);
      }
   }
   long mode = AccountInfoInteger(ACCOUNT_MARGIN_MODE);
   m.S("account.terminalId", g_terminalId);
   m.S("account.login", IntegerToString(AccountInfoInteger(ACCOUNT_LOGIN)));
   m.S("account.server", AccountInfoString(ACCOUNT_SERVER));
   m.S("currency", AccountInfoString(ACCOUNT_CURRENCY));
   m.N("balance", AccountInfoDouble(ACCOUNT_BALANCE), 2);
   m.N("equity", AccountInfoDouble(ACCOUNT_EQUITY), 2);
   m.N("margin", AccountInfoDouble(ACCOUNT_MARGIN), 2);
   m.N("freeMargin", AccountInfoDouble(ACCOUNT_MARGIN_FREE), 2);
   m.S("marginMode", mode == ACCOUNT_MARGIN_MODE_RETAIL_HEDGING ? "hedging" : "netting");
   m.B("tradeAllowed", TradingAllowed());
   m.B("connected", TerminalInfoInteger(TERMINAL_CONNECTED) != 0);
   m.I("copierOpenPositions", positions);
   m.I("copierPendingOrders", pendings);
   m.N("copierOpenLots", lots, 2);
   m.N("copierDailyRealized", realized, 2);
   if(mode == ACCOUNT_MARGIN_MODE_RETAIL_HEDGING)
   {
      // Hedging: only netting accounts need unrelated-exposure checks; drop the list.
      string t = m.text;
      int p;
      while((p = StringFind(t, "payload.netExposureSymbols.")) >= 0)
      {
         int e = StringFind(t, "\n", p);
         t = StringSubstr(t, 0, p) + StringSubstr(t, e + 1);
      }
      m.text = t;
   }
   Send(m);
}

void SendSymbols()
{
   Msg m("SYMBOL_SNAPSHOT");
   for(int i = 0; i < ArraySize(g_symbols); i++)
   {
      string s = g_symbols[i];
      string p = "symbols." + IntegerToString(i) + ".";
      MqlTick tick;
      bool hasTick = SymbolInfoTick(s, tick);
      double margin = 0;
      if(hasTick && !OrderCalcMargin(ORDER_TYPE_BUY, s, 1.0, tick.ask, margin)) margin = 0;
      long tradeMode = SymbolInfoInteger(s, SYMBOL_TRADE_MODE);
      bool open = hasTick && (TimeTradeServer() - tick.time) < 120 && tradeMode != SYMBOL_TRADE_MODE_DISABLED;
      long filling = SymbolInfoInteger(s, SYMBOL_FILLING_MODE);
      m.S(p + "name", s);
      m.I(p + "digits", SymbolInfoInteger(s, SYMBOL_DIGITS));
      m.N(p + "point", SymbolInfoDouble(s, SYMBOL_POINT), 10);
      m.N(p + "tickSize", SymbolInfoDouble(s, SYMBOL_TRADE_TICK_SIZE), 10);
      m.N(p + "tickValue", SymbolInfoDouble(s, SYMBOL_TRADE_TICK_VALUE_LOSS), 8);
      m.N(p + "contractSize", SymbolInfoDouble(s, SYMBOL_TRADE_CONTRACT_SIZE), 2);
      m.N(p + "volumeMin", SymbolInfoDouble(s, SYMBOL_VOLUME_MIN), 4);
      m.N(p + "volumeMax", SymbolInfoDouble(s, SYMBOL_VOLUME_MAX), 4);
      m.N(p + "volumeStep", SymbolInfoDouble(s, SYMBOL_VOLUME_STEP), 4);
      m.I(p + "stopsLevel", SymbolInfoInteger(s, SYMBOL_TRADE_STOPS_LEVEL));
      m.I(p + "freezeLevel", SymbolInfoInteger(s, SYMBOL_TRADE_FREEZE_LEVEL));
      m.B(p + "tradeAllowed", tradeMode == SYMBOL_TRADE_MODE_FULL);
      m.B(p + "sessionOpen", open);
      m.I(p + "fillingModes", filling | 4);
      m.I(p + "expirationModes", SymbolInfoInteger(s, SYMBOL_EXPIRATION_MODE));
      m.N(p + "marginPerLot", margin, 2);
   }
   Send(m);
}

// Full catalog of every symbol the broker offers (names only), so the app can show a complete list the
// user picks from - not just the streamed/Market-Watch symbols.
void SendSymbolCatalog()
{
   int total = SymbolsTotal(false);   // false = ALL broker symbols, not just Market Watch
   if(total > 8000) total = 8000;     // safety cap on huge symbol trees
   // Chunked: one message per ~200 names keeps every message well under the bridge's 64 KB limit, which a
   // full broker catalog (hundreds/thousands of symbols) would otherwise blow past and be dropped.
   const int chunk = 200;
   for(int start = 0; start < total || start == 0; start += chunk)
   {
      Msg m("SYMBOL_CATALOG");
      m.B("reset", start == 0);                       // first chunk resets the catalog on the engine
      m.B("final", start + chunk >= total);
      int k = 0;
      for(int i = start; i < total && i < start + chunk; i++)
         m.S("names." + IntegerToString(k++), SymbolName(i, false));
      Send(m);
      if(total == 0) break;
   }
}

void SendQuotes()
{
   Msg m("QUOTE");
   int k = 0;
   for(int i = 0; i < ArraySize(g_symbols); i++)
   {
      MqlTick t;
      if(!SymbolInfoTick(g_symbols[i], t) || t.bid <= 0 || t.ask <= 0) continue;
      if(t.bid == g_lastBid[i] && t.ask == g_lastAsk[i]) continue;   // unchanged: nothing to send
      g_lastBid[i] = t.bid;
      g_lastAsk[i] = t.ask;
      int digits = (int)SymbolInfoInteger(g_symbols[i], SYMBOL_DIGITS);
      string p = "quotes." + IntegerToString(k++) + ".";
      m.S(p + "symbol", g_symbols[i]);
      m.N(p + "bid", t.bid, digits);
      m.N(p + "ask", t.ask, digits);
      m.I(p + "brokerTimeMs", (long)t.time_msc);
   }
   if(k > 0) Send(m);   // quote updates may coalesce; they are sent only on the timer
}

// command execution
void Execute()
{
   string cmd = Get("payload.commandId");
   if(cmd == "") return;
   // Redelivered command: report its journaled state instead of executing twice.
   string js = JournalState(cmd);
   if(js != "")
   {
      string st = js == "Accepted" ? "Submitted" : js;
      Status(cmd, st, "duplicate delivery ignored (journaled " + js + ")");
      return;
   }
   RememberCommand(cmd);
   Status(cmd, "ReceivedByEA", "");

   string sym = Get("payload.symbol");
   long   login = StringToInteger(Get("payload.account.login"));
   if(login != AccountInfoInteger(ACCOUNT_LOGIN) || Get("payload.account.server") != AccountInfoString(ACCOUNT_SERVER) || Get("payload.account.terminalId") != g_terminalId)
   { Reject(cmd, "account mismatch: command is bound to another terminal/login/server"); return; }
   if(g_paused) { Reject(cmd, "EA paused: unsubmitted command rejected"); return; }
   if(NowMs() > (ulong)GetL("payload.deadlineMs") + 1000) { Reject(cmd, "deadline passed before submission"); return; }
   if(!TradingAllowed()) { Reject(cmd, "trading not allowed (Algo Trading / account permissions)"); return; }
   if(!SymbolSelect(sym, true)) { Reject(cmd, "symbol not available: " + sym); return; }
   Subscribe(sym);
   MqlTick tick;
   if(!SymbolInfoTick(sym, tick) || tick.bid <= 0) { Reject(cmd, "no quote"); return; }
   if(TimeTradeServer() - tick.time > InpMaxTickAgeSec) { Reject(cmd, "stale quote at EA revalidation"); return; }
   long tradeMode = SymbolInfoInteger(sym, SYMBOL_TRADE_MODE);
   bool buy = Get("payload.side") == "BUY";
   if(tradeMode == SYMBOL_TRADE_MODE_DISABLED || tradeMode == SYMBOL_TRADE_MODE_CLOSEONLY || (buy && tradeMode == SYMBOL_TRADE_MODE_SHORTONLY) || (!buy && tradeMode == SYMBOL_TRADE_MODE_LONGONLY))
   { Reject(cmd, "symbol trade mode does not permit this order"); return; }

   double point = SymbolInfoDouble(sym, SYMBOL_POINT);
   double spread = tick.ask - tick.bid;
   if(spread > GetD("payload.maxSpreadPrice") + point * 0.5) { Reject(cmd, "spread " + D(spread, 6) + " above cap at EA revalidation"); return; }
   string kind = Get("payload.orderKind");
   double exec = buy ? tick.ask : tick.bid;       // BUY at ask, SELL at bid
   double lo = GetD("payload.entryLow"), hi = GetD("payload.entryHigh");
   if(kind == "MARKET" && lo > 0 && (exec < lo - point * 0.5 || exec > hi + point * 0.5))
   { Reject(cmd, "price " + D(exec, 6) + " left the permitted range at EA revalidation (not chased)"); return; }

   double vmin = SymbolInfoDouble(sym, SYMBOL_VOLUME_MIN), vmax = SymbolInfoDouble(sym, SYMBOL_VOLUME_MAX), vstep = SymbolInfoDouble(sym, SYMBOL_VOLUME_STEP);
   double volume = FloorToStep(GetD("payload.volume"), vstep);
   if(volume < vmin - 1e-9) { Reject(cmd, "volume below broker minimum; never rounded up"); return; }
   if(volume > vmax) volume = FloorToStep(vmax, vstep);

   double price = kind == "MARKET" ? exec : NormalizeToTick(sym, GetD("payload.pendingPrice"));
   double sl = NormalizeToTick(sym, GetD("payload.stopLoss"));
   double tp = GetD("payload.takeProfit") > 0 ? NormalizeToTick(sym, GetD("payload.takeProfit")) : 0;
   if(sl <= 0) { Reject(cmd, "stop loss is required"); return; }
   if(buy ? !(sl < price) : !(sl > price)) { Reject(cmd, "stop loss on the wrong side of the execution price"); return; }
   if(tp > 0 && (buy ? !(tp > price) : !(tp < price))) { Reject(cmd, "take profit already reached or on the wrong side"); return; }
   double stops = SymbolInfoInteger(sym, SYMBOL_TRADE_STOPS_LEVEL) * point;
   if(MathAbs(price - sl) < stops || (tp > 0 && MathAbs(price - tp) < stops)) { Reject(cmd, "SL/TP inside the broker stops level; stops are never widened"); return; }

   // Authoritative risk check with the broker's own profit calculation.
   double loss = 0;
   if(OrderCalcProfit(buy ? ORDER_TYPE_BUY : ORDER_TYPE_SELL, sym, volume, price, sl, loss))
   {
      double planned = GetD("payload.plannedRiskMoney");
      double share = volume / MathMax(1e-9, GetD("payload.volume"));
      if(planned > 0 && MathAbs(loss) > planned * share * 1.10 + 0.01)
      { Reject(cmd, "loss at SL (" + D(MathAbs(loss), 2) + ") exceeds the planned risk after broker revalidation"); return; }
   }

   MqlTradeRequest req;
   MqlTradeResult  res;
   MqlTradeCheckResult chk;
   ZeroMemory(req); ZeroMemory(res); ZeroMemory(chk);
   req.symbol = sym;
   req.volume = volume;
   req.sl = sl;
   req.tp = tp;
   req.magic = COPIER_MAGIC;
   req.comment = "SR " + StringSubstr(cmd, 0, 8);
   req.deviation = (ulong)MathMax(0, MathRound(GetD("payload.maxDeviationPrice") / point));
   if(kind == "MARKET")
   {
      req.action = TRADE_ACTION_DEAL;
      req.type = buy ? ORDER_TYPE_BUY : ORDER_TYPE_SELL;
      req.price = price;
      req.type_filling = PickFilling(sym);
   }
   else
   {
      req.action = TRADE_ACTION_PENDING;
      if(kind == "LIMIT") req.type = buy ? ORDER_TYPE_BUY_LIMIT : ORDER_TYPE_SELL_LIMIT;
      else req.type = buy ? ORDER_TYPE_BUY_STOP : ORDER_TYPE_SELL_STOP;
      req.price = price;
      req.type_filling = ORDER_FILLING_RETURN;
      long expModes = SymbolInfoInteger(sym, SYMBOL_EXPIRATION_MODE);
      if((expModes & SYMBOL_EXPIRATION_SPECIFIED) == 0) { Reject(cmd, "broker does not support specified expiry; permanent orders are never left silently"); return; }
      long offset = (long)(TimeTradeServer() - TimeGMT());          // broker/server time conversion
      req.type_time = ORDER_TIME_SPECIFIED;
      req.expiration = (datetime)(GetL("payload.pendingExpiryMs") / 1000 + offset);
      double freeze = SymbolInfoInteger(sym, SYMBOL_TRADE_FREEZE_LEVEL) * point;
      if(MathAbs(price - exec) < MathMax(stops, freeze)) { Reject(cmd, "pending price inside stops/freeze distance"); return; }
   }
   if(!OrderCheck(req, chk) && chk.retcode != 0)
   { Reject(cmd, "OrderCheck: " + IntegerToString(chk.retcode) + " " + chk.comment); return; }

   // Durable acceptance record BEFORE broker submission. Without it the command is not sent.
   if(BridgeJournalAppend(cmd, "Accepted", sym) != 1) { Status(cmd, "Rejected", "journal failure: not submitted"); return; }
   long sendUs = BridgeNowMicros();
   if(!OrderSendAsync(req, res))
   {
      BridgeJournalAppend(cmd, "Rejected", "OrderSendAsync " + IntegerToString(res.retcode));
      Status(cmd, "Rejected", "OrderSendAsync failed: " + IntegerToString(res.retcode) + " " + res.comment);
      return;
   }
   MapRequest(res.request_id, cmd);
   BridgeJournalAppend(cmd, "Submitted", IntegerToString(res.request_id));
   // Submission is not a fill: the outcome arrives through OnTradeTransaction.
   Status(cmd, "Submitted", "OrderSendAsync accepted for submission", 0, 0, sendUs, res.request_id);
   AddOrderSlot(cmd, volume);   // ticket filled in when the request result arrives
}

// reconciliation
void Reconcile()
{
   Msg m("RECONCILE_REPORT");
   int n = (int)GetL("payload.commands.count");
   datetime from = TimeCurrent() - 7 * 86400;
   HistorySelect(from, TimeCurrent() + 60);
   for(int i = 0; i < n; i++)
   {
      string cmd = Get("payload.commands." + IntegerToString(i) + ".commandId");
      string js = JournalState(cmd);
      string state = "";
      ulong ticket = 0;
      double filled = 0;
      string prefix = "SR " + StringSubstr(cmd, 0, 8);
      for(int d = HistoryDealsTotal() - 1; d >= 0; d--)
      {
         ulong t = HistoryDealGetTicket(d);
         if(HistoryDealGetInteger(t, DEAL_MAGIC) != COPIER_MAGIC || HistoryDealGetInteger(t, DEAL_ENTRY) != DEAL_ENTRY_IN) continue;
         if(StringFind(HistoryDealGetString(t, DEAL_COMMENT), prefix) != 0) continue;
         filled += HistoryDealGetDouble(t, DEAL_VOLUME);
         ticket = (ulong)HistoryDealGetInteger(t, DEAL_ORDER);
         state = "Filled";
      }
      if(state == "")
         for(int o = OrdersTotal() - 1; o >= 0; o--)
         {
            ulong t = OrderGetTicket(o);
            if(OrderGetInteger(ORDER_MAGIC) == COPIER_MAGIC && StringFind(OrderGetString(ORDER_COMMENT), prefix) == 0) { state = "Working"; ticket = t; }
         }
      if(state == "" && js == "Rejected") state = "Rejected";
      string p = "commands." + IntegerToString(i) + ".";
      m.S(p + "commandId", cmd);
      m.B(p + "journaled", js != "");
      m.S(p + "state", state == "" ? "Unknown" : state);
      if(ticket > 0) m.S(p + "orderTicket", IntegerToString((long)ticket));
      m.N(p + "filledVolume", filled, 2);
      if(js != "") RememberCommand(cmd);
   }
   // Recent copier deals so the local ledger can reconcile totals (deduplicated by ticket).
   int k = 0;
   for(int d = HistoryDealsTotal() - 1; d >= 0 && k < 40; d--)
   {
      ulong t = HistoryDealGetTicket(d);
      if(HistoryDealGetInteger(t, DEAL_MAGIC) != COPIER_MAGIC) continue;
      AppendDeal(m, "deals." + IntegerToString(k++) + ".", t);
   }
   Send(m);
}

void AppendDeal(Msg &m, string p, ulong t)
{
   m.S(p + "ticket", IntegerToString((long)t));
   m.S(p + "order", IntegerToString(HistoryDealGetInteger(t, DEAL_ORDER)));
   m.S(p + "positionId", IntegerToString(HistoryDealGetInteger(t, DEAL_POSITION_ID)));
   m.I(p + "timeMs", HistoryDealGetInteger(t, DEAL_TIME_MSC));
   m.S(p + "symbol", HistoryDealGetString(t, DEAL_SYMBOL));
   m.I(p + "type", HistoryDealGetInteger(t, DEAL_TYPE));
   m.I(p + "entry", HistoryDealGetInteger(t, DEAL_ENTRY));
   m.N(p + "volume", HistoryDealGetDouble(t, DEAL_VOLUME), 2);
   m.N(p + "price", HistoryDealGetDouble(t, DEAL_PRICE), 6);
   m.N(p + "profit", HistoryDealGetDouble(t, DEAL_PROFIT), 2);
   m.N(p + "commission", HistoryDealGetDouble(t, DEAL_COMMISSION), 2);
   m.N(p + "swap", HistoryDealGetDouble(t, DEAL_SWAP), 2);
   m.N(p + "fee", HistoryDealGetDouble(t, DEAL_FEE), 2);
   m.I(p + "magic", HistoryDealGetInteger(t, DEAL_MAGIC));
   string cmd = "";
   int oi = OrderIndex((ulong)HistoryDealGetInteger(t, DEAL_ORDER));
   if(oi >= 0) cmd = g_orderCmd[oi];
   if(cmd == "") cmd = CmdForComment(HistoryDealGetString(t, DEAL_COMMENT));
   m.S(p + "commandId", cmd);
}

// explicit user actions (copier-owned only)
void Manage()
{
   string action = Get("payload.action");
   MqlTradeRequest req;
   MqlTradeResult res;
   if(action == "close_positions")
   {
      for(int i = PositionsTotal() - 1; i >= 0; i--)
      {
         ulong t = PositionGetTicket(i);
         if(t == 0 || PositionGetInteger(POSITION_MAGIC) != COPIER_MAGIC) continue;
         ZeroMemory(req); ZeroMemory(res);
         string sym = PositionGetString(POSITION_SYMBOL);
         bool isBuy = PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY;
         req.action = TRADE_ACTION_DEAL;
         req.position = t;
         req.symbol = sym;
         req.volume = PositionGetDouble(POSITION_VOLUME);
         req.type = isBuy ? ORDER_TYPE_SELL : ORDER_TYPE_BUY;
         req.price = isBuy ? SymbolInfoDouble(sym, SYMBOL_BID) : SymbolInfoDouble(sym, SYMBOL_ASK);
         req.magic = COPIER_MAGIC;
         req.deviation = 50;
         req.type_filling = PickFilling(sym);
         req.comment = "SR close";
         if(!OrderSendAsync(req, res)) Print("Pipkite: close failed ", res.retcode);
      }
   }
   else if(action == "cancel_pending")
   {
      for(int i = OrdersTotal() - 1; i >= 0; i--)
      {
         ulong t = OrderGetTicket(i);
         if(t == 0 || OrderGetInteger(ORDER_MAGIC) != COPIER_MAGIC) continue;
         ZeroMemory(req); ZeroMemory(res);
         req.action = TRADE_ACTION_REMOVE;
         req.order = t;
         if(!OrderSendAsync(req, res)) Print("Pipkite: cancel failed ", res.retcode);
      }
   }
   SendAccount();
}

// transaction processing (outside OnTradeTransaction)
void PushEvent(int type, ulong a, ulong b, int c)
{
   int next = (g_evTail + 1) % MAX_EVENTS;
   if(next == g_evHead) { Print("Pipkite: transaction event buffer full; reconciliation will recover"); return; }
   g_evType[g_evTail] = type; g_evA[g_evTail] = a; g_evB[g_evTail] = b; g_evC[g_evTail] = c;
   g_evTail = next;
}

void ProcessEvents()
{
   int processed = 0;
   while(g_evHead != g_evTail && processed < 32)
   {
      int type = g_evType[g_evHead];
      ulong a = g_evA[g_evHead], b = g_evB[g_evHead];
      int c = g_evC[g_evHead];
      g_evHead = (g_evHead + 1) % MAX_EVENTS;
      processed++;
      if(type == 1)   // request result: a=request_id, b=order, c=retcode
      {
         string cmd = CmdForRequest(a);
         if(cmd == "") continue;
         for(int i = ArraySize(g_orders) - 1; i >= 0; i--)
            if(g_orders[i] == 0 && g_orderCmd[i] == cmd) { g_orders[i] = b; break; }
         if(c == TRADE_RETCODE_DONE || c == TRADE_RETCODE_DONE_PARTIAL) Status(cmd, "Accepted", "broker accepted (retcode " + IntegerToString(c) + ")", 0, b);
         else if(c == TRADE_RETCODE_PLACED) { BridgeJournalAppend(cmd, "Working", IntegerToString((long)b)); Status(cmd, "Working", "pending order placed", 0, b); }
         else { BridgeJournalAppend(cmd, "Rejected", IntegerToString(c)); Status(cmd, "Rejected", "broker rejected: retcode " + IntegerToString(c), 0, b); }
      }
      else if(type == 2)   // deal added: a=deal ticket
      {
         if(!HistoryDealSelect(a)) continue;
         if(HistoryDealGetInteger(a, DEAL_MAGIC) != COPIER_MAGIC) continue;
         Msg m("TRADE_EVENT");
         m.S("kind", "deal");
         AppendDeal(m, "deal.", a);
         Send(m);
         if(HistoryDealGetInteger(a, DEAL_ENTRY) == DEAL_ENTRY_IN)
         {
            int oi = OrderIndex((ulong)HistoryDealGetInteger(a, DEAL_ORDER));
            string cmd = oi >= 0 ? g_orderCmd[oi] : CmdForComment(HistoryDealGetString(a, DEAL_COMMENT));
            if(cmd == "") continue;
            if(oi >= 0)
            {
               g_orderFilled[oi] += HistoryDealGetDouble(a, DEAL_VOLUME);
               bool full = g_orderFilled[oi] >= g_orderVol[oi] - 1e-9;
               if(full) BridgeJournalAppend(cmd, "Filled", IntegerToString((long)a));
               Status(cmd, full ? "Filled" : "PartiallyFilled", "deal " + IntegerToString((long)a), g_orderFilled[oi], g_orders[oi]);
            }
            else Status(cmd, "Filled", "deal " + IntegerToString((long)a), HistoryDealGetDouble(a, DEAL_VOLUME), (ulong)HistoryDealGetInteger(a, DEAL_ORDER));
         }
      }
      else if(type == 3)   // order finished: a=order ticket, c=state
      {
         int oi = OrderIndex(a);
         if(oi < 0) continue;
         string cmd = g_orderCmd[oi];
         if(c == ORDER_STATE_CANCELED) { BridgeJournalAppend(cmd, "Canceled", ""); Status(cmd, "Canceled", "order canceled", g_orderFilled[oi], a); }
         else if(c == ORDER_STATE_EXPIRED) { BridgeJournalAppend(cmd, "Expired", ""); Status(cmd, "Expired", "pending order expired", g_orderFilled[oi], a); }
         else if(c == ORDER_STATE_REJECTED) { BridgeJournalAppend(cmd, "Rejected", ""); Status(cmd, "Rejected", "order rejected by broker", g_orderFilled[oi], a); }
      }
   }
}

//+------------------------------------------------------------------+
int OnInit()
{
   if(!TerminalInfoInteger(TERMINAL_DLLS_ALLOWED))
   {
      Alert("Pipkite: enable 'Allow DLL imports' (Tools > Options > Expert Advisors) and reattach the EA.");
      return INIT_FAILED;
   }
   long login = AccountInfoInteger(ACCOUNT_LOGIN);
   // Exactly one copier EA per terminal/account: a second chart is refused.
   g_ownerKey = "Pipkite.Owner." + IntegerToString(login);
   if(GlobalVariableCheck(g_ownerKey))
   {
      long owner = (long)GlobalVariableGet(g_ownerKey);
      if(owner != ChartID() && ChartGetInteger(owner, CHART_WINDOW_HANDLE) != 0)
      {
         Alert("Pipkite: another copier EA is already attached for this account (chart ", owner, "). Only one is allowed.");
         g_ownerKey = "";
         return INIT_FAILED;
      }
   }
   GlobalVariableTemp(g_ownerKey);
   GlobalVariableSet(g_ownerKey, (double)ChartID());

   g_terminalId = "MT5-" + Fnv1a(TerminalInfoString(TERMINAL_DATA_PATH));
   ArrayResize(g_rx, 65536);
   int rc = BridgeInitialize(g_terminalId, login, AccountInfoString(ACCOUNT_SERVER));
   if(rc < 0)
   {
      string text;
      StringInit(text, 512, 0);
      BridgeStatusText(text, 512);
      Alert("Pipkite bridge could not start (", rc, "): ", text, ". Start Pipkite on this Windows account first.");
      return INIT_FAILED;
   }
   Subscribe(_Symbol);
   string extra[];
   int n = StringSplit(InpExtraSymbols, ',', extra);
   for(int i = 0; i < n; i++) { StringTrimLeft(extra[i]); StringTrimRight(extra[i]); Subscribe(extra[i]); }
   EventSetMillisecondTimer(MathMax(10, InpTimerMs));
   g_ready = true;
   PrintFormat("Pipkite EA 1.0.0 started: terminal %s, account %I64d @ %s, host ABI %d", g_terminalId, login, AccountInfoString(ACCOUNT_SERVER), BridgeVersion());
   return INIT_SUCCEEDED;
}

void OnDeinit(const int reason)
{
   EventKillTimer();
   if(g_ready) BridgeShutdown();
   if(g_ownerKey != "" && GlobalVariableCheck(g_ownerKey) && (long)GlobalVariableGet(g_ownerKey) == ChartID()) GlobalVariableDel(g_ownerKey);
   Comment("");
}

// Drains the bridge queue (bounded batch). Called from OnTimer and OnTick so commands are picked up on
// whichever event comes first; BridgeTryReceive never blocks on pipe I/O.
void ProcessInbound()
{
   // Bounded batch; BridgeTryReceive never blocks on pipe I/O.
   for(int i = 0; i < InpMaxBatch; i++)
   {
      int n = BridgeTryReceive(g_rx, ArraySize(g_rx));
      if(n < 0) { ArrayResize(g_rx, -n + 16); n = BridgeTryReceive(g_rx, ArraySize(g_rx)); }
      if(n <= 0) break;
      string text = CharArrayToString(g_rx, 0, n, CP_UTF8);
      Parse(text);
      string type = Get("type");
      if(type == "EXECUTE") Execute();
      else if(type == "SNAPSHOT_REQUEST")
      {
         int count = (int)GetL("payload.symbols.count");
         for(int k = 0; k < count; k++) Subscribe(Get("payload.symbols." + IntegerToString(k)));
         SendAccount(); SendSymbols(); SendQuotes(); SendSymbolCatalog();
      }
      else if(type == "RECONCILE_REQUEST") Reconcile();
      else if(type == "PAUSE") { g_paused = Get("payload.paused") == "true"; PrintFormat("Pipkite: pause=%s", g_paused ? "on" : "off"); }
      else if(type == "MANAGE") Manage();
      else if(type == "WELCOME" || type == "ERROR") Print("Pipkite: ", text);
   }
}

void OnTick()
{
   if(!g_ready) return;
   ProcessInbound();
   ProcessEvents();
   FlushOutbox();
}

void OnTimer()
{
   if(!g_ready) return;
   ProcessInbound();
   ProcessEvents();
   FlushOutbox();

   ulong now = GetTickCount64();
   int status = BridgeConnectionStatus();
   if(status >= 2)
   {
      if(now - g_lastQuoteMs >= (ulong)InpQuoteMs) { SendQuotes(); g_lastQuoteMs = now; }
      if(now - g_lastHeartbeatMs >= 1000) { Msg hb("HEARTBEAT"); hb.I("eaTimeMs", (long)NowMs()); Send(hb); g_lastHeartbeatMs = now; }
      if(now - g_lastAccountMs >= 2000) { SendAccount(); g_lastAccountMs = now; }
   }
   if(now - g_lastStatusMs >= 1000)
   {
      string text;
      StringInit(text, 512, 0);
      BridgeStatusText(text, 512);
      Comment("Pipkite 1.0.0\n", text, g_paused ? "\nPAUSED" : "", "\nMagic ", COPIER_MAGIC);
      g_lastStatusMs = now;
   }
}

// Kept short: record the event and reconcile outside the handler. Events may arrive in several
// stages without a fixed ordering.
void OnTradeTransaction(const MqlTradeTransaction &trans, const MqlTradeRequest &request, const MqlTradeResult &result)
{
   if(trans.type == TRADE_TRANSACTION_REQUEST) PushEvent(1, result.request_id, result.order, (int)result.retcode);
   else if(trans.type == TRADE_TRANSACTION_DEAL_ADD) PushEvent(2, trans.deal, 0, 0);
   else if(trans.type == TRADE_TRANSACTION_HISTORY_ADD && (trans.order_state == ORDER_STATE_CANCELED || trans.order_state == ORDER_STATE_EXPIRED || trans.order_state == ORDER_STATE_REJECTED))
      PushEvent(3, trans.order, 0, (int)trans.order_state);
}
//+------------------------------------------------------------------+

#pragma once
// Canonical, UI-independent models shared by the engine, storage and tests.
// Keep these plain: no Windows types, no UI controls, no broker handles.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sr {

inline constexpr const char* kProductName = "Pipkite";
inline constexpr const char* kProductVersion = "1.0.0";
inline constexpr int kConfigSchemaVersion = 1;
inline constexpr int kProtocolVersion = 1;
inline constexpr std::int64_t kCopierMagic = 20260930;  // MT5 magic number for copier-owned orders

enum class ParseStatus { Valid, Incomplete, Ambiguous, Unsupported, Invalid };
enum class MessageKind { TradeSetup, Commentary, Management, NonText, Empty };
enum class Side { Buy, Sell };
enum class OrderKind { Market, Limit, Stop };
enum class EntryMode { WaitForRange, ImmediateWithinTolerance, ExplicitPendingOnly };
enum class ExecutionMode { Preview, Automatic };
enum class AiMode { Off, Advisory, LocalGate, BackendGate };
enum class AiVerdict { Good, Bad, Uncertain, Unavailable };
enum class MarginMode { Hedging, Netting };

struct SourceKey {
    std::int64_t chatId = 0;
    std::int64_t messageId = 0;
    bool operator==(const SourceKey&) const = default;
};

struct PriceRange {
    double low = 0;
    double high = 0;
};

// Byte offsets [begin, end) into the ORIGINAL message text (not the normalized copy).
struct Span {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

struct Evidence {
    std::string field;  // symbol, side, kind, now, entry, sl, tp1..tpN, lot, ref, expiry
    Span span;
};

struct TakeProfit {
    int index = 0;  // 1-based label index; 0 when the provider did not number it
    double price = 0;
    Span span;
};

struct Signal {
    SourceKey source;
    int revision = 0;  // Telegram edit version
    std::string rawText;
    std::string symbolAlias;  // text as the provider wrote it, e.g. GOLD
    std::string symbol;       // canonical symbol after template alias mapping, e.g. XAUUSD
    std::optional<Side> side;
    OrderKind orderKind = OrderKind::Market;
    bool explicitKind = false;  // provider wrote LIMIT or STOP explicitly
    bool marketIntent = false;  // provider wrote NOW / MARKET
    std::optional<PriceRange> entry;
    std::optional<double> stopLoss;
    std::vector<TakeProfit> targets;
    std::optional<double> lot;  // ignored unless policy.allowMessageLot
    std::string reference;
    std::optional<int> expiryMinutes;
    std::int64_t publishedAtMs = 0;
    std::int64_t receivedAtMs = 0;
    std::vector<Evidence> evidence;
    ParseStatus status = ParseStatus::Invalid;
    MessageKind kind = MessageKind::Empty;
    std::vector<std::string> diagnostics;
    std::string parserId;
    std::string parserVersion;
    double parseMicros = 0;
};

struct AccountKey {
    std::string terminalId;
    std::int64_t login = 0;
    std::string server;
    bool empty() const { return terminalId.empty() || login == 0 || server.empty(); }
    std::string str() const { return terminalId + "|" + std::to_string(login) + "|" + server; }
    bool operator==(const AccountKey&) const = default;
};

struct SymbolSpec {
    std::string name;
    int digits = 5;
    double point = 0.00001;
    double tickSize = 0.00001;
    double tickValue = 1.0;  // account-currency value of one tick for 1.0 lot (loss side)
    double contractSize = 100000;
    double volumeMin = 0.01;
    double volumeMax = 100;
    double volumeStep = 0.01;
    int stopsLevelPoints = 0;
    int freezeLevelPoints = 0;
    bool tradeAllowed = true;   // SYMBOL_TRADE_MODE_FULL
    bool sessionOpen = true;
    int fillingModes = 1;       // bitmask: 1 FOK, 2 IOC, 4 RETURN
    int expirationModes = 15;   // bitmask: 1 GTC, 2 DAY, 4 SPECIFIED, 8 SPECIFIED_DAY
    double marginPerLot = 0;    // 0 = unknown; EA OrderCheck remains authoritative
};

struct Quote {
    std::string symbol;
    double bid = 0;
    double ask = 0;
    std::int64_t brokerTimeMs = 0;
    std::int64_t receivedAtMs = 0;  // engine wall clock at receipt
};

struct AccountSnapshot {
    AccountKey key;
    std::string currency = "USD";
    double balance = 0;
    double equity = 0;
    double margin = 0;
    double freeMargin = 0;
    MarginMode marginMode = MarginMode::Hedging;
    bool tradeAllowed = false;
    bool connected = false;
    bool simulated = false;  // true when the peer is the fake terminal harness
    int copierOpenPositions = 0;
    int copierPendingOrders = 0;
    double copierOpenLots = 0;
    double copierDailyRealized = 0;  // negative = loss, includes fees
    std::vector<std::string> netExposureSymbols;  // netting: symbols with any open position
    std::vector<std::string> copierSymbols;       // symbols with copier-owned positions
    std::int64_t updatedAtMs = 0;
};

struct ChildOrder {
    int index = 0;
    double volume = 0;
    std::optional<double> takeProfit;
    int tpIndex = 0;
};

struct ExecutionPlan {
    std::string planId;
    std::int64_t configVersion = 0;
    AccountKey account;
    SourceKey source;
    int revision = 0;
    std::string parserVersion;
    std::string brokerSymbol;
    Side side = Side::Buy;
    OrderKind kind = OrderKind::Market;
    std::optional<PriceRange> entry;
    double pendingPrice = 0;  // only for ExplicitPendingOnly LIMIT/STOP
    double stopLoss = 0;
    double totalVolume = 0;
    std::vector<ChildOrder> children;
    std::int64_t expiryMs = 0;       // execution must happen before this instant
    std::int64_t pendingExpiryMs = 0;  // broker pending expiry, 0 for market
    double maxDeviationPrice = 0;
    double maxSpreadPrice = 0;
    double plannedRiskMoney = 0;
    double referencePrice = 0;  // quote used for the decision
    std::string sizingMode;
    double riskPercent = 0;
    std::int64_t createdAtMs = 0;
};

enum class CommandState {
    Prepared, Dispatching, ReceivedByEA, Submitted, Accepted, PartiallyFilled, Filled,
    Rejected, Unknown, Working, Canceled, Expired
};

enum class SignalState { Received, Parsed, Review, Rejected, Validated, Waiting, Planned, Previewed, Expired, Dispatched, Ignored, Historical };

struct Command {
    std::string commandId;  // durable UUID
    std::string planId;
    int childIndex = 0;
    std::string action;  // BUY, SELL LIMIT, ...
    std::int64_t deadlineMs = 0;
    CommandState state = CommandState::Prepared;
    std::string payloadJson;
};

struct Deal {
    std::uint64_t ticket = 0;
    std::uint64_t order = 0;
    std::uint64_t positionId = 0;
    std::int64_t timeMs = 0;
    std::string symbol;
    int type = 0;   // MT5 DEAL_TYPE: 0 buy, 1 sell, 2 balance, 3 credit, ...
    int entry = 0;  // MT5 DEAL_ENTRY: 0 in, 1 out, 2 inout, 3 out_by
    double volume = 0;
    double price = 0;
    double profit = 0;
    double commission = 0;
    double swap = 0;
    double fee = 0;
    std::int64_t magic = 0;
    std::string commandId;  // resolved attribution; empty when unresolved
    std::int64_t sourceChatId = 0;
};

// enum <-> string helpers (stable identifiers used in JSON, DB and IPC)
const char* toString(ParseStatus v);
const char* toString(MessageKind v);
const char* toString(Side v);
const char* toString(OrderKind v);
const char* toString(EntryMode v);
const char* toString(ExecutionMode v);
const char* toString(AiMode v);
const char* toString(AiVerdict v);
const char* toString(CommandState v);
const char* toString(SignalState v);
const char* toString(MarginMode v);

std::optional<EntryMode> entryModeFromString(const std::string& s);
std::optional<ExecutionMode> executionModeFromString(const std::string& s);
std::optional<AiMode> aiModeFromString(const std::string& s);
std::optional<CommandState> commandStateFromString(const std::string& s);
std::optional<ParseStatus> parseStatusFromString(const std::string& s);

// "BUY", "SELL LIMIT", "BUY STOP" ...
std::string actionString(Side side, OrderKind kind);

}  // namespace sr

#include "Models.h"

namespace sr {

const char* toString(ParseStatus v) {
    switch (v) {
        case ParseStatus::Valid: return "Valid";
        case ParseStatus::Incomplete: return "Incomplete";
        case ParseStatus::Ambiguous: return "Ambiguous";
        case ParseStatus::Unsupported: return "Unsupported";
        case ParseStatus::Invalid: return "Invalid";
    }
    return "Invalid";
}

const char* toString(MessageKind v) {
    switch (v) {
        case MessageKind::TradeSetup: return "TradeSetup";
        case MessageKind::Commentary: return "Commentary";
        case MessageKind::Management: return "Management";
        case MessageKind::NonText: return "NonText";
        case MessageKind::Empty: return "Empty";
    }
    return "Empty";
}

const char* toString(Side v) { return v == Side::Buy ? "BUY" : "SELL"; }

const char* toString(OrderKind v) {
    switch (v) {
        case OrderKind::Market: return "MARKET";
        case OrderKind::Limit: return "LIMIT";
        case OrderKind::Stop: return "STOP";
    }
    return "MARKET";
}

const char* toString(EntryMode v) {
    switch (v) {
        case EntryMode::WaitForRange: return "wait_for_range";
        case EntryMode::ImmediateWithinTolerance: return "immediate_within_tolerance";
        case EntryMode::ExplicitPendingOnly: return "explicit_pending_only";
    }
    return "wait_for_range";
}

const char* toString(ExecutionMode v) { return v == ExecutionMode::Automatic ? "automatic" : "preview"; }

const char* toString(AiMode v) {
    switch (v) {
        case AiMode::Off: return "off";
        case AiMode::Advisory: return "advisory";
        case AiMode::LocalGate: return "local_gate";
        case AiMode::BackendGate: return "backend_gate";
    }
    return "off";
}

const char* toString(AiVerdict v) {
    switch (v) {
        case AiVerdict::Good: return "Good";
        case AiVerdict::Bad: return "Bad";
        case AiVerdict::Uncertain: return "Uncertain";
        case AiVerdict::Unavailable: return "Unavailable";
    }
    return "Unavailable";
}

const char* toString(CommandState v) {
    switch (v) {
        case CommandState::Prepared: return "Prepared";
        case CommandState::Dispatching: return "Dispatching";
        case CommandState::ReceivedByEA: return "ReceivedByEA";
        case CommandState::Submitted: return "Submitted";
        case CommandState::Accepted: return "Accepted";
        case CommandState::PartiallyFilled: return "PartiallyFilled";
        case CommandState::Filled: return "Filled";
        case CommandState::Rejected: return "Rejected";
        case CommandState::Unknown: return "Unknown";
        case CommandState::Working: return "Working";
        case CommandState::Canceled: return "Canceled";
        case CommandState::Expired: return "Expired";
    }
    return "Unknown";
}

const char* toString(SignalState v) {
    switch (v) {
        case SignalState::Received: return "Received";
        case SignalState::Parsed: return "Parsed";
        case SignalState::Review: return "Review";
        case SignalState::Rejected: return "Rejected";
        case SignalState::Validated: return "Validated";
        case SignalState::Waiting: return "Waiting";
        case SignalState::Planned: return "Planned";
        case SignalState::Previewed: return "Previewed";
        case SignalState::Expired: return "Expired";
        case SignalState::Dispatched: return "Dispatched";
        case SignalState::Ignored: return "Ignored";
        case SignalState::Historical: return "Historical";
    }
    return "Received";
}

const char* toString(MarginMode v) { return v == MarginMode::Netting ? "netting" : "hedging"; }

std::optional<EntryMode> entryModeFromString(const std::string& s) {
    if (s == "wait_for_range") return EntryMode::WaitForRange;
    if (s == "immediate_within_tolerance") return EntryMode::ImmediateWithinTolerance;
    if (s == "explicit_pending_only") return EntryMode::ExplicitPendingOnly;
    return std::nullopt;
}

std::optional<ExecutionMode> executionModeFromString(const std::string& s) {
    if (s == "preview") return ExecutionMode::Preview;
    if (s == "automatic") return ExecutionMode::Automatic;
    return std::nullopt;
}

std::optional<AiMode> aiModeFromString(const std::string& s) {
    if (s == "off") return AiMode::Off;
    if (s == "advisory") return AiMode::Advisory;
    if (s == "local_gate") return AiMode::LocalGate;
    if (s == "backend_gate") return AiMode::BackendGate;
    return std::nullopt;
}

std::optional<CommandState> commandStateFromString(const std::string& s) {
    for (int i = 0; i <= static_cast<int>(CommandState::Expired); ++i) {
        auto st = static_cast<CommandState>(i);
        if (s == toString(st)) return st;
    }
    return std::nullopt;
}

std::optional<ParseStatus> parseStatusFromString(const std::string& s) {
    for (int i = 0; i <= static_cast<int>(ParseStatus::Invalid); ++i) {
        auto st = static_cast<ParseStatus>(i);
        if (s == toString(st)) return st;
    }
    return std::nullopt;
}

std::string actionString(Side side, OrderKind kind) {
    std::string s = toString(side);
    if (kind == OrderKind::Limit) s += " LIMIT";
    if (kind == OrderKind::Stop) s += " STOP";
    return s;
}

}  // namespace sr

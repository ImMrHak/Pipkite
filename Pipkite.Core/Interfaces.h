#pragma once
// Core seams. Production implementations live in the engine; fakes live in tests and the demo harness.

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Clock.h"
#include "Models.h"
#include "Planner.h"
#include "Result.h"
#include "SignalParser.h"

namespace sr {

struct IncomingMessage {
    std::int64_t chatId = 0;
    std::int64_t messageId = 0;
    std::int64_t editVersion = 0;  // 0 = original; edit_date for edits
    std::int64_t senderId = 0;
    std::int64_t replyToMessageId = 0;
    std::int64_t publishedAtMs = 0;
    std::int64_t receivedAtMs = 0;
    std::int64_t receivedMonoUs = 0;
    std::string text;
    bool hasMedia = false;
    std::string mediaKind;   // photo, voice, document ...
    bool historical = false; // loaded from history/catch-up rather than a live push
    bool simulated = false;  // injected by the demo harness
};

struct ChatInfo {
    std::int64_t chatId = 0;
    std::string title;
    std::string type;  // channel | group | supergroup | private
};

enum class TelegramAuthState { NotConfigured, WaitParameters, WaitPhone, WaitCode, WaitPassword, Ready, LoggingOut, Closed, LibraryMissing, Error };
const char* toString(TelegramAuthState s);

class ITelegramSource {
public:
    virtual ~ITelegramSource() = default;
    virtual void start(std::function<void(IncomingMessage)> onMessage) = 0;
    virtual void stop() = 0;
    virtual TelegramAuthState authState() const = 0;
    virtual std::vector<ChatInfo> chats() const = 0;
};

class IExecutionJournal {
public:
    virtual ~IExecutionJournal() = default;
    // Durable before returning: the submission boundary is never crossed without this record.
    virtual Status recordCommand(const Command& c, const ExecutionPlan& plan) = 0;
    virtual Status transition(const std::string& commandId, CommandState to, const std::string& detailJson) = 0;
    virtual std::vector<Command> unresolvedCommands() = 0;
};

class IMetaTraderGateway {
public:
    virtual ~IMetaTraderGateway() = default;
    virtual bool healthy() const = 0;
    virtual std::optional<AccountSnapshot> account() const = 0;
    virtual std::optional<SymbolSpec> symbol(const std::string& name) const = 0;
    virtual std::optional<Quote> quote(const std::string& name) const = 0;
    // Enqueue only; never blocks on I/O. False = backpressure/not connected.
    virtual bool dispatch(const Command& c) = 0;
};

}  // namespace sr

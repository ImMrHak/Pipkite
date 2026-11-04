#pragma once
// AI help for a channel whose signals no format reads. The AI never reads or decides a trade: it suggests words and
// settings to add to a format, and Pipkite checks the result on the channel's messages like a taught format before
// the user can pick it. Trading never waits on an AI.
// The backend has the same prompt (backend/src/main/resources/ai/format-prompt.md); change both together.

#include <string>
#include <vector>

#include "ParserTemplate.h"
#include "Result.h"
#include <nlohmann/json.hpp>

namespace sr {

// System prompt: what the AI must answer.
const std::string& formatSuggestionPrompt();

// User message: what the base format already knows, then the (redacted) messages.
std::string formatSuggestionRequest(const ParserTemplate& base, const std::vector<std::string>& messages);

// What the base format knows, compact, for the backend's AI endpoint ({"buyWords": [...], ...}).
nlohmann::json knownWords(const ParserTemplate& base);

// Request body for an OpenAI-compatible chat completions API (OpenAI, DeepSeek, ...).
nlohmann::json chatCompletionBody(const std::string& model, const std::string& system, const std::string& user);

// The JSON object a chat completions reply carries in choices[0].message.content.
Result<nlohmann::json> suggestionFromChatReply(const std::string& body);

struct AppliedSuggestion {
    ParserTemplate format;
    std::vector<std::string> added;  // one plain line per change, for the user
    std::string explanation;         // the AI's own summary (shown as its words, never trusted)
};

// base + the suggestion's additions. Only word lists, symbol aliases, emoji labels and the two number settings can
// change; the safety rules (SL and TP required, conflicts go to review, maxTargets, advanced rules) never do.
// Anything malformed, too long or contradictory is skipped and reported, never applied.
Result<AppliedSuggestion> applyFormatSuggestion(const ParserTemplate& base, const nlohmann::json& suggestion);

}  // namespace sr

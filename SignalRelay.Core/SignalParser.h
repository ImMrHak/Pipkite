#pragma once
// utf-8 check -> normalize -> template match -> extract -> semantic checks -> Signal (+ evidence spans)

#include <memory>
#include <string_view>

#include "Models.h"
#include "ParserTemplate.h"

namespace sr {

struct ParseContext {
    SourceKey source;
    int revision = 0;
    std::int64_t publishedAtMs = 0;
    std::int64_t receivedAtMs = 0;
    bool hasMedia = false;
};

class ISignalParser {
public:
    virtual ~ISignalParser() = default;
    virtual Signal parse(std::string_view rawText, const ParseContext& ctx) const = 0;
    virtual std::string id() const = 0;
    virtual std::string version() const = 0;
};

// Parser backed by a compiled declarative template.
class TemplateSignalParser final : public ISignalParser {
public:
    explicit TemplateSignalParser(std::shared_ptr<const CompiledTemplate> t) : t_(std::move(t)) {}
    Signal parse(std::string_view rawText, const ParseContext& ctx) const override;
    std::string id() const override { return t_->source.id; }
    std::string version() const override { return t_->source.version; }
    const CompiledTemplate& compiled() const { return *t_; }

private:
    std::shared_ptr<const CompiledTemplate> t_;
};

Signal parseWithTemplate(std::string_view rawText, const CompiledTemplate& t, const ParseContext& ctx);

// Validates price geometry: BUY needs SL below entry and TPs above; SELL the reverse.
// Returns an empty string when valid, otherwise a human-readable reason.
std::string checkGeometry(const Signal& s);

}  // namespace sr

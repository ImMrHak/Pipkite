#include <algorithm>
#include <filesystem>
#include <fstream>

#include "Pipkite.Core/BoundedRegex.h"
#include "Pipkite.Core/ParserBuilder.h"
#include "Pipkite.Core/SignalParser.h"
#include "Pipkite.Core/Text.h"
#include "TestFramework.h"

using namespace sr;

namespace {

std::shared_ptr<const CompiledTemplate> gold_();
std::shared_ptr<const CompiledTemplate> gold() { return gold_(); }
std::shared_ptr<const CompiledTemplate> gold_() {
    static auto c = CompiledTemplate::compile(starterGoldTemplate()).value();
    return c;
}

Signal parseGold(const std::string& text) {
    ParseContext ctx;
    return parseWithTemplate(text, *gold(), ctx);
}

std::string evidenceText(const Signal& s, const std::string& field) {
    for (auto& e : s.evidence)
        if (e.field == field) return s.rawText.substr(e.span.begin, e.span.end - e.span.begin);
    return "<none>";
}

}  // namespace

TEST(parser_spec_fixture_gold_buy_range) {
    auto s = parseGold("#GOLD BUY 4300 4295\nTP1 4305\nTP2 4310\nTP3 4315\nTP4 4320\nTP5 4325\nTP6 4330\nSL 4287");
    CHECK_EQ(std::string(toString(s.status)), std::string("Valid"));
    CHECK(s.side && *s.side == Side::Buy);
    CHECK_EQ(s.symbol, std::string("XAUUSD"));
    CHECK_EQ(s.symbolAlias, std::string("GOLD"));
    CHECK(s.entry.has_value());
    CHECK_EQ(s.entry->low, 4295.0);
    CHECK_EQ(s.entry->high, 4300.0);
    CHECK_EQ(s.targets.size(), std::size_t(6));
    CHECK_EQ(s.targets[5].price, 4330.0);
    CHECK_EQ(*s.stopLoss, 4287.0);
    CHECK(!s.marketIntent);
    CHECK_EQ(evidenceText(s, "sl"), std::string("4287"));
    CHECK_EQ(evidenceText(s, "symbol"), std::string("GOLD"));
}

TEST(parser_spec_fixture_gold_sell_now_range) {
    auto s = parseGold("GOLD (XAUUSD) Sell NOW\nEntry 4302–4306\nTP1 4296\nTP2 4288\nTP3 4282\nTP4 4276\nSL 4316");
    CHECK_EQ(std::string(toString(s.status)), std::string("Valid"));
    CHECK(*s.side == Side::Sell);
    CHECK(s.marketIntent);
    CHECK_EQ(s.entry->low, 4302.0);
    CHECK_EQ(s.entry->high, 4306.0);
    CHECK_EQ(s.targets.size(), std::size_t(4));
    CHECK_EQ(*s.stopLoss, 4316.0);
    CHECK_EQ(evidenceText(s, "now"), std::string("NOW"));
}

TEST(parser_spec_fixture_glued_sell_accepted_only_by_tested_template) {
    auto s = parseGold("GOLD SELL4295\nTP1 4285\nTP2 4280\nSL 4305");
    CHECK_EQ(std::string(toString(s.status)), std::string("Valid"));
    CHECK_EQ(s.entry->low, 4295.0);
    CHECK_EQ(s.entry->high, 4295.0);
    auto strict = CompiledTemplate::compile(starterStrictTemplate()).value();
    ParseContext ctx;
    auto s2 = parseWithTemplate("GOLD SELL4295\nTP1 4285\nTP2 4280\nSL 4305", *strict, ctx);
    CHECK(s2.status != ParseStatus::Valid);
}

TEST(parser_commentary_is_not_an_entry) {
    for (const char* text : {"TP1 HIT", "great profit", "buy or sell?", "\xE2\x9C\x85 TP2 HIT +80 pips"}) {
        auto s = parseGold(text);
        CHECK(s.status != ParseStatus::Valid);
        CHECK_EQ(outcomeOf(s), std::string("ignore"));
    }
}

TEST(parser_both_directions_ambiguous) {
    auto s = parseGold("GOLD BUY 4300 SELL 4310\nSL 4290\nTP 4320");
    CHECK_EQ(std::string(toString(s.status)), std::string("Ambiguous"));
}

TEST(parser_entry_only_then_reply_not_executable) {
    auto a = parseGold("GOLD BUY 4300");
    CHECK_EQ(std::string(toString(a.status)), std::string("Incomplete"));
    auto b = parseGold("SL 4290\nTP1 4310");
    CHECK_EQ(std::string(toString(b.status)), std::string("Incomplete"));
}

TEST(parser_unicode_variants) {
    // Superscript TP label, NBSP, CRLF, Markdown, emoji, Arabic-Indic digits, en dash.
    std::string text = "\xF0\x9F\x9F\xA1 *GOLD* BUY\xC2\xA0" "4300 - 4295\r\nTP\xC2\xB9 4305\r\nTP\xC2\xB2 \xD9\xA4\xD9\xA3\xD9\xA1\xD9\xA0\r\nSL: 4287";
    auto s = parseGold(text);
    CHECK_EQ(std::string(toString(s.status)), std::string("Valid"));
    CHECK_EQ(s.targets.size(), std::size_t(2));
    CHECK_EQ(s.targets[0].index, 1);
    CHECK_EQ(s.targets[1].price, 4310.0);
    CHECK_EQ(evidenceText(s, "tp2"), std::string("\xD9\xA4\xD9\xA3\xD9\xA1\xD9\xA0"));  // highlight maps to the original Arabic digits
    CHECK_EQ(evidenceText(s, "symbol"), std::string("GOLD"));
}

TEST(parser_tp_label_variants_normalize) {
    for (const char* tp : {"TP1 4305", "TP 1 4305", "TP\xC2\xB9 4305", "TP1: 4305", "Tp 1 - 4305", "TP1\xEF\xB8\x8F\xE2\x83\xA3 4305"}) {
        auto s = parseGold(std::string("XAUUSD BUY 4300\n") + tp + "\nSL 4290");
        CHECK_EQ(std::string(toString(s.status)), std::string("Valid"));
        CHECK_EQ(s.targets.size(), std::size_t(1));
        CHECK_EQ(s.targets[0].price, 4305.0);
    }
}

TEST(parser_conflicts_require_review) {
    CHECK_EQ(std::string(toString(parseGold("GOLD BUY 4300\nSL 4290\nSL 4280\nTP 4310").status)), std::string("Ambiguous"));
    CHECK_EQ(std::string(toString(parseGold("GOLD BUY 4300\nSL 4290\nTP1 4310\nTP1 4320").status)), std::string("Ambiguous"));
    CHECK_EQ(std::string(toString(parseGold("GOLD EURUSD BUY 4300\nSL 4290\nTP 4310").status)), std::string("Ambiguous"));
    CHECK_EQ(std::string(toString(parseGold("GOLD BUY 4300\nSL 4290\nTP 4310\nGOLD BUY 4250\nSL 4240\nTP 4260").status)), std::string("Ambiguous"));
}

TEST(parser_geometry_invalid) {
    auto s = parseGold("GOLD BUY 4300\nSL 4310\nTP 4320");
    CHECK_EQ(std::string(toString(s.status)), std::string("Invalid"));
    auto t = parseGold("GOLD SELL 4300\nSL 4310\nTP 4320");
    CHECK_EQ(std::string(toString(t.status)), std::string("Invalid"));
}

TEST(parser_numbers_locale_independent_and_strict) {
    TokenizeOptions dot;
    CHECK(!parseStrictNumber("4,300", dot).has_value());  // ambiguous without explicit permission
    CHECK_EQ(*parseStrictNumber("4300.55", dot), 4300.55);
    TokenizeOptions dotThousands;
    dotThousands.allowThousandsSeparator = true;
    CHECK_EQ(*parseStrictNumber("4,300.5", dotThousands), 4300.5);
    CHECK(!parseStrictNumber("43,00", dotThousands).has_value());
    TokenizeOptions comma;
    comma.numberFormat = NumberFormat::CommaDecimal;
    CHECK_EQ(*parseStrictNumber("1,0850", comma), 1.085);
    CHECK(!parseStrictNumber("1234567890123456", dot).has_value());  // > 15 significant digits
    auto s = parseGold("GOLD BUY 4,300\nSL 4290\nTP 4310");
    CHECK(s.status != ParseStatus::Valid);
}

TEST(parser_limits_enforced) {
    std::string big(70 * 1024, 'A');
    CHECK_EQ(std::string(toString(parseGold(big).status)), std::string("Invalid"));
    std::string badUtf8 = "GOLD BUY \xC0\xAF 4300";
    CHECK_EQ(std::string(toString(parseGold(badUtf8).status)), std::string("Invalid"));
    std::string longLine(5000, 'B');
    CHECK_EQ(std::string(toString(parseGold(longLine).status)), std::string("Invalid"));
}

TEST(parser_pending_and_management) {
    auto s = parseGold("XAUUSD BUY LIMIT 4290\nSL 4280\nTP 4310");
    CHECK_EQ(std::string(toString(s.status)), std::string("Valid"));
    CHECK(s.explicitKind && s.orderKind == OrderKind::Limit);
    auto stop = parseGold("XAUUSD SELL STOP 4280\nSTOP LOSS 4295\nTAKE PROFIT 4260");
    CHECK_EQ(std::string(toString(stop.status)), std::string("Valid"));
    CHECK(stop.orderKind == OrderKind::Stop);
    CHECK_EQ(*stop.stopLoss, 4295.0);
    auto m = parseGold("Move SL to BE");
    CHECK(m.kind == MessageKind::Management);
    CHECK(m.status == ParseStatus::Unsupported);
}

TEST(parser_pips_are_informational) {
    auto s = parseGold("GOLD BUY 4300\nTP1 4305 (50 pips)\nTP2 100 pips\nSL 4290 (100 pips)");
    CHECK_EQ(std::string(toString(s.status)), std::string("Valid"));
    CHECK_EQ(s.targets.size(), std::size_t(1));
    CHECK_EQ(*s.stopLoss, 4290.0);
}

TEST(parser_message_lot_extracted_but_flagged_by_policy_later) {
    auto s = parseGold("EURUSD SELL 1.0850\nSL 1.0880\nTP 1.0800\nLot 0.05");
    CHECK_EQ(std::string(toString(s.status)), std::string("Valid"));
    CHECK(s.lot && *s.lot == 0.05);
    CHECK_EQ(s.symbol, std::string("EURUSD"));
}

TEST(parser_media_without_text_is_non_executable) {
    ParseContext ctx;
    ctx.hasMedia = true;
    auto s = parseWithTemplate("", *gold(), ctx);
    CHECK(s.kind == MessageKind::NonText);
    CHECK(s.status == ParseStatus::Unsupported);
}

TEST(parser_multilingual_presets) {
    auto t = starterGoldTemplate();
    applyArabicPreset(t);
    auto c = CompiledTemplate::compile(t).value();
    ParseContext ctx;
    auto s = parseWithTemplate("\xD8\xA7\xD9\x84\xD8\xB0\xD9\x87\xD8\xA8 \xD8\xB4\xD8\xB1\xD8\xA7\xD8\xA1 4300\n\xD9\x88\xD9\x82\xD9\x81 \xD8\xA7\xD9\x84\xD8\xAE\xD8\xB3\xD8\xA7\xD8\xB1\xD8\xA9 4290\n\xD9\x87\xD8\xAF\xD9\x81 4310", *c, ctx);
    CHECK_EQ(std::string(toString(s.status)), std::string("Valid"));
    CHECK_EQ(s.symbol, std::string("XAUUSD"));
}

TEST(parser_required_fixture_suite_passes) {
    TemplateSignalParser p(gold());
    auto suite = runFixtures(p, requiredSpecFixtures());
    for (auto& r : suite.results)
        if (!r.passed) std::printf("        fixture %s: %s\n", r.id.c_str(), r.differences.empty() ? "" : r.differences[0].c_str());
    CHECK(suite.allPassed());
    CHECK(suite.passed >= 14);
}

TEST(parser_throughput_over_1000_per_second) {
    TemplateSignalParser p(gold());
    auto fixtures = requiredSpecFixtures();
    const int n = 20000;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) {
        ParseContext ctx;
        p.parse(fixtures[i % fixtures.size()].text, ctx);
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(n / secs > 1000.0);
}

TEST(regex_bounded_basic) {
    auto re = BoundedRegex::compile("SL\\s*[:=]?\\s*(\\d+(?:\\.\\d+)?)").value();
    auto m = re->search("GOLD BUY\nSL = 4287.5\nTP 1");
    CHECK(m.has_value());
    CHECK(m->groups[1].has_value());
    auto [b, e] = *m->groups[1];
    CHECK_EQ(std::string("GOLD BUY\nSL = 4287.5\nTP 1").substr(b, e - b), std::string("4287.5"));
    CHECK(!BoundedRegex::compile("(a)\\1").ok());
    CHECK(!BoundedRegex::compile("(?=a)").ok());
    CHECK(!BoundedRegex::compile("a**").ok());
    auto alt = BoundedRegex::compile("^(BUY|SELL) (\\w+)$").value();
    auto m2 = alt->search("x\nsell gold");
    CHECK(m2.has_value());
}

TEST(regex_no_catastrophic_backtracking) {
    auto re = BoundedRegex::compile("(a+)+b").value();
    std::string input(20000, 'a');
    auto t0 = std::chrono::steady_clock::now();
    auto m = re->search(input);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(!m.has_value());
    CHECK(ms < 2000.0);
}

TEST(parser_advanced_rule_fills_missing_field) {
    auto t = starterStrictTemplate();
    t.id = "adv_test";
    t.slLabels = {"SL"};
    t.advancedRules.push_back({"sl", "PROTECT(?:ION)?\\s+AT\\s+(\\d+)"});
    auto c = CompiledTemplate::compile(t).value();
    ParseContext ctx;
    auto s = parseWithTemplate("XAUUSD BUY 4300\nProtection at 4290\nTP 4310", *c, ctx);
    CHECK_EQ(std::string(toString(s.status)), std::string("Valid"));
    CHECK_EQ(*s.stopLoss, 4290.0);
}

TEST(builder_learns_labels_from_highlights) {
    auto base = starterStrictTemplate();
    base.id = "learned";
    std::string sample = "XAUUSD BUY 4300\nProtect 4290\nGoal 4310";
    std::vector<HighlightLabel> labels{{"sl", 24, 28, ""}, {"tp", 34, 38, ""}};
    CHECK_EQ(sample.substr(24, 4), std::string("4290"));
    CHECK_EQ(sample.substr(34, 4), std::string("4310"));
    auto r = learnFromHighlights(base, sample, labels);
    CHECK_EQ(r.learned.size(), std::size_t(2));
    auto c = CompiledTemplate::compile(r.updated).value();
    ParseContext ctx;
    auto s = parseWithTemplate(sample, *c, ctx);
    CHECK_EQ(std::string(toString(s.status)), std::string("Valid"));
    CHECK_EQ(*s.stopLoss, 4290.0);
    CHECK_EQ(s.targets[0].price, 4310.0);
}

TEST(registry_publish_activate_rollback_and_leases) {
    ParserRegistry reg;
    auto t1 = starterGoldTemplate();
    CHECK(reg.publish(t1, "builtin", 1).ok());
    CHECK(reg.activate(t1.id, "1.0.0", 1).ok());
    auto leaseOld = reg.lease(t1.id);
    auto t2 = t1;
    t2.version = "1.1.0";
    t2.tpLabels.push_back("GOAL");
    CHECK(reg.publish(t2, "local", 2).ok());
    auto t2bad = t2;
    t2bad.slLabels.push_back("X");
    CHECK(!reg.publish(t2bad, "local", 3).ok());  // immutable versions
    CHECK(reg.activate(t2.id, "1.1.0", 3).ok());
    auto leaseNew = reg.lease(t1.id);
    CHECK_EQ(leaseOld->version, std::string("1.0.0"));  // old lease still valid and unchanged
    CHECK_EQ(leaseNew->version, std::string("1.1.0"));
    CHECK(leaseNew->generation > leaseOld->generation);
    auto rb = reg.rollback(t1.id, 4);
    CHECK(rb.ok());
    CHECK_EQ(rb.value(), std::string("1.0.0"));
    CHECK_EQ(reg.lease(t1.id)->version, std::string("1.0.0"));
}

TEST(template_json_roundtrip_and_validation) {
    auto t = starterGoldTemplate();
    auto j = t.toJson();
    auto back = ParserTemplate::fromJson(j);
    CHECK(back.ok());
    CHECK(back.value().toJson() == j);
    j["schemaVersion"] = 99;
    CHECK(!ParserTemplate::fromJson(j).ok());
    auto bad = t;
    bad.sellWords.push_back("BUY");
    CHECK(!bad.validate().ok());
}

TEST(compare_parsers_reports_field_changes) {
    auto t1 = starterStrictTemplate();
    auto t2 = t1;
    t2.version = "1.0.1";
    t2.tpLabels.push_back("GOAL");
    TemplateSignalParser a(CompiledTemplate::compile(t1).value()), b(CompiledTemplate::compile(t2).value());
    auto diffs = compareParsers(a, b, {"XAUUSD BUY 4300\nSL 4290\nGOAL 4310"});
    CHECK_EQ(diffs.size(), std::size_t(1));
    CHECK(std::find(diffs[0].changedFields.begin(), diffs[0].changedFields.end(), "status") != diffs[0].changedFields.end());
}

TEST(shipped_samples_match_code) {
    // config/templates and Pipkite.Tests/fixtures are generated by `Pipkite.Tests.exe --export-samples <repo>`.
    auto root = std::filesystem::path(__FILE__).parent_path().parent_path();
    auto text = [&](const std::filesystem::path& p) -> std::string {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };
    auto gold = text(root / "config" / "templates" / "gold_provider_v1.json");
    if (gold.empty()) return;  // not running from a source checkout
    CHECK(nlohmann::json::parse(gold) == starterGoldTemplate().toJson());
    auto fx = nlohmann::json::parse(text(root / "Pipkite.Tests" / "fixtures" / "spec_fixtures.json"));
    TemplateSignalParser p(gold_());
    std::vector<Fixture> fixtures;
    for (auto& f : fx) fixtures.push_back(Fixture::fromJson(f));
    CHECK(runFixtures(p, fixtures).allPassed());
}

// gold_parser_native 1.0.0, sample plugin for the C ABI (query/create/parse/healthcheck/destroy)

#include <windows.h>

#include <cstring>
#include <memory>
#include <string>

#include "SignalRelay.Core/JsonModels.h"
#include "SignalRelay.Core/ParserTemplate.h"
#include "SignalRelay.Core/SignalParser.h"
#include "SignalRelayPluginAbi.h"

namespace {

const SrPluginInfo kInfo{SR_PLUGIN_ABI_VERSION, "parser", "gold_parser_native", "1.0.0"};

struct Instance {
    std::shared_ptr<const sr::CompiledTemplate> rules;
};

std::shared_ptr<const sr::CompiledTemplate> buildRules() {
    auto t = sr::starterGoldTemplate();
    t.id = "gold_parser_native";
    t.version = "1.0.0";
    t.name = "Native gold transformer";
    t.symbolAliases["XAU"] = "XAUUSD";
    t.symbolAliases["GOLDSPOT"] = "XAUUSD";
    t.tpLabels.push_back("TGT");
    t.slLabels.push_back("SLL");
    auto c = sr::CompiledTemplate::compile(t);
    return c ? c.value() : nullptr;
}

}  // namespace

extern "C" {

__declspec(dllexport) const SrPluginInfo* SrPluginQuery() { return &kInfo; }

__declspec(dllexport) void* SrPluginCreate(const char*, int32_t) {
    try {
        auto inst = new Instance();
        inst->rules = buildRules();
        if (!inst->rules) {
            delete inst;
            return nullptr;
        }
        return inst;
    } catch (...) {
        return nullptr;  // exceptions never cross the ABI
    }
}

__declspec(dllexport) int32_t SrPluginParse(void* instance, const char* text, int32_t len, char* out, int32_t cap) {
    try {
        auto* inst = static_cast<Instance*>(instance);
        if (!inst || !text || len < 0) return 0;
        sr::ParseContext ctx;
        sr::Signal s = sr::parseWithTemplate(std::string_view(text, static_cast<std::size_t>(len)), *inst->rules, ctx);
        std::string js = sr::toJson(s).dump();
        if (static_cast<std::size_t>(cap) < js.size()) return -static_cast<int32_t>(js.size());
        std::memcpy(out, js.data(), js.size());
        return static_cast<int32_t>(js.size());
    } catch (...) {
        return 0;
    }
}

__declspec(dllexport) int32_t SrPluginHealthcheck(void* instance) {
    char buf[8192];
    const char* sample = "GOLD BUY 4300\nSL 4290\nTP 4310";
    int32_t n = SrPluginParse(instance, sample, static_cast<int32_t>(std::strlen(sample)), buf, sizeof buf);
    return (n > 0 && std::string(buf, static_cast<std::size_t>(n)).find("\"Valid\"") != std::string::npos) ? 1 : 0;
}

__declspec(dllexport) void SrPluginDestroy(void* instance) { delete static_cast<Instance*>(instance); }

}  // extern "C"

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }

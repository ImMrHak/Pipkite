// Generic template parser for the C ABI (query/create/parse/healthcheck/destroy).
// One build serves every custom parser: the module's identity and its rules come from the
// "template" of the manifest.json next to it, which the host has verified (signature + this
// DLL's hash) before loading it. tools\build-parser.ps1 writes and signs that manifest.

#include <windows.h>

#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#include "Pipkite.Core/JsonModels.h"
#include "Pipkite.Core/ParserTemplate.h"
#include "Pipkite.Core/SignalParser.h"
#include "PipkitePluginAbi.h"

namespace {

struct Loaded {
    std::string id, version;
    std::shared_ptr<const sr::CompiledTemplate> rules;  // null when the manifest is unusable
    SrPluginInfo info{SR_PLUGIN_ABI_VERSION, "parser", "", ""};
};

std::once_flag gOnce;
Loaded gLoaded;

std::wstring ownDirectory() {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ownDirectory), &self))
        return L"";
    wchar_t path[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(self, path, static_cast<DWORD>(std::size(path)));
    if (n == 0 || n >= std::size(path)) return L"";
    std::wstring p(path, n);
    auto slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : p.substr(0, slash);
}

void load() {
    std::ifstream f(ownDirectory() + L"\\manifest.json", std::ios::binary);
    if (!f) return;
    std::stringstream text;
    text << f.rdbuf();
    auto j = nlohmann::json::parse(text.str(), nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;
    gLoaded.id = j.value("id", "");
    gLoaded.version = j.value("version", "");
    gLoaded.info.id = gLoaded.id.c_str();
    gLoaded.info.version = gLoaded.version.c_str();
    auto t = sr::ParserTemplate::fromJson(j.value("template", nlohmann::json::object()));
    if (!t) return;
    auto tpl = t.value();
    tpl.id = gLoaded.id;
    tpl.version = gLoaded.version;
    auto c = sr::CompiledTemplate::compile(tpl);
    if (c) gLoaded.rules = c.value();
}

const Loaded& loaded() {
    std::call_once(gOnce, [] {
        try {
            load();
        } catch (...) {
            // an unusable manifest leaves no rules: create and the healthcheck fail
        }
    });
    return gLoaded;
}

}  // namespace

extern "C" {

__declspec(dllexport) const SrPluginInfo* SrPluginQuery() { return &loaded().info; }

__declspec(dllexport) void* SrPluginCreate(const char*, int32_t) {
    // The instance is the shared, immutable rules; nothing to allocate per instance.
    auto& l = loaded();
    return l.rules ? const_cast<sr::CompiledTemplate*>(l.rules.get()) : nullptr;
}

__declspec(dllexport) int32_t SrPluginParse(void* instance, const char* text, int32_t len, char* out, int32_t cap) {
    try {
        auto* rules = static_cast<const sr::CompiledTemplate*>(instance);
        if (!rules || !text || len < 0) return 0;
        sr::ParseContext ctx;
        sr::Signal s = sr::parseWithTemplate(std::string_view(text, static_cast<std::size_t>(len)), *rules, ctx);
        std::string js = sr::toJson(s).dump();
        if (static_cast<std::size_t>(cap) < js.size()) return -static_cast<int32_t>(js.size());
        std::memcpy(out, js.data(), js.size());
        return static_cast<int32_t>(js.size());
    } catch (...) {
        return 0;  // exceptions never cross the ABI
    }
}

// The rules compiled; the manifest's fixtures prove they parse this channel's messages.
__declspec(dllexport) int32_t SrPluginHealthcheck(void* instance) { return instance ? 1 : 0; }

__declspec(dllexport) void SrPluginDestroy(void*) {}

}  // extern "C"

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }

// SignalRelay unit/integration test runner. Usage: SignalRelay.Tests.exe [filter-substring]
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>

#include "SignalRelay.Core/ParserBuilder.h"
#include "SignalRelay.Protocol/Platform.h"
#include "TestFramework.h"

// Writes the starter templates and the specification fixture corpus as JSON (config/templates, SignalRelay.Tests/fixtures).
static int exportSamples(const std::string& root) {
    using namespace sr;
    for (auto t : {starterGoldTemplate(), starterForexTemplate(), starterStrictTemplate()})
        platform::writeFileAtomic(platform::widen(root + "/config/templates/" + t.id + ".json"), t.toJson().dump(2) + "\n");
    nlohmann::json fx = nlohmann::json::array();
    for (auto& f : requiredSpecFixtures()) fx.push_back(f.toJson());
    platform::writeFileAtomic(platform::widen(root + "/SignalRelay.Tests/fixtures/spec_fixtures.json"), fx.dump(2) + "\n");
    std::printf("samples exported to %s\n", root.c_str());
    return 0;
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc > 2 && std::strcmp(argv[1], "--export-samples") == 0) return exportSamples(argv[2]);
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int passed = 0, failed = 0;
    const auto start = std::chrono::steady_clock::now();
    for (auto& t : srtest::registry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        try {
            t.fn();
            ++passed;
            std::printf("  PASS  %s\n", t.name);
        } catch (const srtest::Failure& f) {
            ++failed;
            std::printf("  FAIL  %s\n        %s\n", t.name, f.message.c_str());
        } catch (const std::exception& e) {
            ++failed;
            std::printf("  FAIL  %s\n        unexpected exception: %s\n", t.name, e.what());
        }
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("\n%d passed, %d failed, %d checks, %.2f s\n", passed, failed, srtest::checkCount(), secs);
    return failed == 0 ? 0 : 1;
}

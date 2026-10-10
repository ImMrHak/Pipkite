#pragma once
// finds MT5 terminals, installs the EA and starts the terminal with it on one chart.
// uses terminal64.exe /config:<ini> ([StartUp] opens the chart, [Experts] enables algo trading + DLLs).
// the EA isn't added again if the profile already has it, and a running terminal is never restarted.

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

namespace sr::mt5 {

// Detected terminals: [{dataPath, origin, exePath, portable, hasEA, hasDll, running}].
nlohmann::json detectTerminals();

// Copies PipkiteEA (.ex5/.mq5) and MT5BridgeHost.dll from installDir into the data folder.
// Returns {copied:[...]} or throws std::runtime_error with a user-facing message.
nlohmann::json installEa(const std::filesystem::path& installDir, const std::filesystem::path& dataPath);

struct LaunchRequest {
    std::filesystem::path installDir;  // Pipkite install (EA + bridge DLL source)
    std::filesystem::path appDataDir;  // Pipkite data folder (startup ini is written here)
    std::filesystem::path dataPath;    // terminal data folder (contains MQL5)
    std::filesystem::path exePath;     // terminal64.exe; derived from origin.txt when empty
    std::string chartSymbol = "EURUSD";
};

// Installs the EA if needed and starts the terminal. Returns {state: "started"|"running", attached, exePath,
// message}. Throws std::runtime_error with a user-facing message on failure.
nlohmann::json launch(const LaunchRequest& r);

bool isRunning(const std::filesystem::path& exePath);
std::filesystem::path exeForDataPath(const std::filesystem::path& dataPath);
bool activeProfileHasEa(const std::filesystem::path& dataPath);

}  // namespace sr::mt5

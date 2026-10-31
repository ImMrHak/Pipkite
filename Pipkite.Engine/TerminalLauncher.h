#pragma once
// finds MT5 terminals, installs the EA and starts the terminal with it on one chart.
// uses terminal64.exe /config:<ini> ([StartUp] opens the chart, [Experts] enables algo trading + DLLs).
// the EA isn't added again if the profile already has it, and a running terminal is never restarted.

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace sr::mt5 {

// Detected terminals: [{dataPath, origin, exePath, exeFound, portable, needsFirstRun, hasEA, hasDll, running}].
// Any broker's MT5 counts: data folders under %APPDATA%\MetaQuotes\Terminal, then every installed terminal64.exe
// (uninstall entries, Program Files, extraInstallDirs). needsFirstRun: installed but never opened, so dataPath is empty.
nlohmann::json detectTerminals(const std::vector<std::filesystem::path>& extraInstallDirs = {});
// Same, over explicit folders (dataRoot = the MetaQuotes\Terminal folder).
nlohmann::json detectTerminals(const std::filesystem::path& dataRoot, const std::vector<std::filesystem::path>& installDirs);

// Checks a folder the user chose (the MetaTrader install folder, or its data folder): terminal64.exe present, it is
// MetaTrader 5, its data folder, and that Pipkite can write the EA and bridge there (without that the EA never connects).
// Returns {ok, terminal (detectTerminals entry or null), checks: [{name, ok, detail}]}.
nlohmann::json checkFolder(const std::filesystem::path& folder);
nlohmann::json checkFolder(const std::filesystem::path& folder, const std::filesystem::path& dataRoot);

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

// Installs the EA if needed and starts the terminal. Returns {state: "started"|"running"|"firstRun", attached, exePath,
// message}; "firstRun" (empty dataPath) only opens the terminal so it creates its data folder.
// Throws std::runtime_error with a user-facing message on failure.
nlohmann::json launch(const LaunchRequest& r);

bool isRunning(const std::filesystem::path& exePath);
std::filesystem::path exeForDataPath(const std::filesystem::path& dataPath);
bool activeProfileHasEa(const std::filesystem::path& dataPath);

}  // namespace sr::mt5

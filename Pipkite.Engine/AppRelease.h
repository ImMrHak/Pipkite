#pragma once
// the newest Pipkite release on GitHub, for the "update available" bar. header-only so the tests can use it.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace sr::engine {

inline constexpr const char* kLatestReleaseUrl = "https://api.github.com/repos/ImMrHak/Pipkite/releases/latest";

// -1, 0 or 1. A leading "v" is ignored and missing parts count as 0, so "1.0" == "1.0.0".
inline int compareVersions(std::string a, std::string b) {
    auto parts = [](std::string v) {
        if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
        std::vector<int> p;
        std::size_t start = 0;
        while (start <= v.size()) {
            auto dot = v.find('.', start);
            try {
                p.push_back(std::stoi(v.substr(start, dot - start)));
            } catch (...) {
                p.push_back(0);
            }
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        while (p.size() < 3) p.push_back(0);
        return p;
    };
    auto pa = parts(std::move(a)), pb = parts(std::move(b));
    for (std::size_t i = 0; i < 3; ++i)
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    return 0;
}

// From GitHub's "latest release" JSON: {version, page, setupUrl, setupName} when it is newer than
// installedVersion, otherwise null. setupUrl is empty when the release has no installer.
inline nlohmann::json appReleaseInfo(const nlohmann::json& release, const std::string& installedVersion) {
    if (!release.is_object() || release.value("draft", false) || release.value("prerelease", false)) return nullptr;
    std::string version = release.value("tag_name", "");
    if (!version.empty() && (version[0] == 'v' || version[0] == 'V')) version.erase(0, 1);
    if (version.empty() || version.size() > 20 || version.find_first_not_of("0123456789.") != std::string::npos) return nullptr;
    if (compareVersions(version, installedVersion) <= 0) return nullptr;
    nlohmann::json info = {{"version", version}, {"page", release.value("html_url", "")}, {"setupUrl", ""}, {"setupName", ""}};
    const std::string setup = "Pipkite-" + version + "-setup.exe";
    if (release.contains("assets") && release["assets"].is_array())
        for (const auto& a : release["assets"])
            if (a.is_object() && a.value("name", "") == setup) {
                info["setupUrl"] = a.value("browser_download_url", "");
                info["setupName"] = setup;
            }
    return info;
}

}  // namespace sr::engine

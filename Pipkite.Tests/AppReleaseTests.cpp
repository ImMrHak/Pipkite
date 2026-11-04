// The "update available" bar reads GitHub's latest release: only a newer, final release with an installer is offered.
#include "Pipkite.Engine/AppRelease.h"
#include "TestFramework.h"

using namespace sr::engine;
using nlohmann::json;

namespace {

json release(const std::string& tag) {
    return {{"tag_name", tag},
            {"html_url", "https://github.com/ImMrHak/Pipkite/releases/tag/" + tag},
            {"draft", false},
            {"prerelease", false},
            {"assets", json::array({{{"name", "Pipkite-1.0.10-win-x64.zip"}, {"browser_download_url", "https://example.test/zip"}},
                                    {{"name", "Pipkite-1.0.10-setup.exe"}, {"browser_download_url", "https://example.test/setup"}}})}};
}

}  // namespace

TEST(app_release_newer_version_is_offered_with_its_installer) {
    json info = appReleaseInfo(release("v1.0.10"), "1.0.9");  // 10 > 9, not a text comparison
    CHECK(!info.is_null());
    CHECK_EQ(info.value("version", ""), std::string("1.0.10"));
    CHECK_EQ(info.value("setupUrl", ""), std::string("https://example.test/setup"));
    CHECK_EQ(info.value("setupName", ""), std::string("Pipkite-1.0.10-setup.exe"));
    CHECK_EQ(info.value("page", ""), std::string("https://github.com/ImMrHak/Pipkite/releases/tag/v1.0.10"));
}

TEST(app_release_same_older_draft_or_odd_versions_are_not_offered) {
    CHECK(appReleaseInfo(release("v1.0.10"), "1.0.10").is_null());
    CHECK(appReleaseInfo(release("1.0.2"), "1.0.3").is_null());
    json draft = release("v2.0.0");
    draft["prerelease"] = true;
    CHECK(appReleaseInfo(draft, "1.0.3").is_null());
    CHECK(appReleaseInfo(release("v2.0.0/../x"), "1.0.3").is_null());
    CHECK(appReleaseInfo(json::array(), "1.0.3").is_null());
}

TEST(app_release_without_installer_is_offered_as_a_download_page) {
    json info = appReleaseInfo(release("v2.0.0"), "1.0.3");  // assets are named for 1.0.10, not 2.0.0
    CHECK(!info.is_null());
    CHECK_EQ(info.value("setupUrl", ""), std::string(""));
}

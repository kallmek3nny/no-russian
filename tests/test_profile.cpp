// Profile save layer tests (Sections 14, 17, 22): platform defaults, settings sanitizing, touch
// layout prefs with the 9dp/HUD gate, and research unlocks. JSON persistence is covered below.
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "TestHarness.h"
#include "game/Profile.h"
#include "game/ProfileStore.h"

using namespace lt;
using game::Profile;
using render::RenderTier;

namespace {
const input::TouchButton* findButton(const std::vector<input::TouchButton>& v, input::Action a) {
    for (const input::TouchButton& b : v)
        if (b.action == a) return &b;
    return nullptr;
}
float centreX(const input::TouchButton& b) { return b.rectDp.x + b.rectDp.w * 0.5f; }
} // namespace

int main() {
    {
        TEST_SECTION("platform defaults");
        const game::Settings android = game::defaultSettings(RenderTier::AndroidC31);
        CHECK(android.graphicsTier == RenderTier::AndroidC31);
        CHECK_EQ(android.internalResolution, 640);
        CHECK_EQ(android.fontScale, 1.25f);
        CHECK_EQ(game::defaultSettings(RenderTier::DesktopMedium).internalResolution, 1080);
        game::Settings copy = android;
        CHECK(!game::sanitize(copy)); // defaults are already legal
    }
    {
        TEST_SECTION("settings sanitize");
        game::Settings s = game::defaultSettings(RenderTier::AndroidC31);
        s.internalResolution = 4000;
        s.cameraDistance = 100.0f;
        s.fontScale = std::numeric_limits<float>::quiet_NaN();
        s.audio.master = -0.5f;
        s.colourblind = static_cast<game::ColourblindPalette>(9);
        CHECK(game::sanitize(s));
        CHECK_EQ(s.internalResolution, 720);
        CHECK_EQ(s.cameraDistance, game::kCameraDistanceMax);
        CHECK_EQ(s.fontScale, 1.0f);
        CHECK_EQ(s.audio.master, 0.0f);
        CHECK(s.colourblind == game::ColourblindPalette::Off);
        CHECK(!game::sanitize(s)); // idempotent
    }
    {
        TEST_SECTION("touch prefs and 9dp/HUD gate");
        game::TouchLayoutPrefs t;
        CHECK(t.find(input::Action::Menu) == nullptr);
        t.find(input::Action::Move)->visible = false; // regions can never be hidden
        t.find(input::Action::Jump)->visible = false;
        t.find(input::Action::Guard)->scale = 10.0f;
        CHECK(game::sanitize(t));
        CHECK(t.find(input::Action::Move)->visible);
        CHECK(!t.find(input::Action::Jump)->visible);
        CHECK_EQ(t.find(input::Action::Guard)->scale, game::kTouchScaleMax);

        const auto base = input::defaultTouchLayout(800.0f, 400.0f);
        const auto built = game::buildTouchLayout(t, 1.0f, 800.0f, 400.0f);
        CHECK_EQ(built.size(), base.size());
        const input::TouchButton* g0 = findButton(base, input::Action::Guard);
        const input::TouchButton* g1 = findButton(built, input::Action::Guard);
        CHECK(g0 != nullptr && g1 != nullptr);
        if (g0 != nullptr && g1 != nullptr) {
            CHECK_EQ(g1->rectDp.w, g0->rectDp.w * 2.0f); // scaled about its centre
            CHECK(std::fabs(centreX(*g1) - centreX(*g0)) < 1e-3f);
        }
        const input::TouchButton* jump = findButton(built, input::Action::Jump);
        CHECK(jump != nullptr && !jump->visible);
        CHECK(game::touchLayoutIssues(t, 1.0f, 800.0f, 400.0f, {}).empty());
        const std::vector<input::Rect> fullHud{input::Rect{0.0f, 0.0f, 800.0f, 400.0f}};
        CHECK(!game::touchLayoutIssues(t, 1.0f, 800.0f, 400.0f, fullHud).empty());
    }
    {
        TEST_SECTION("research unlocks");
        Profile p = game::defaultProfile(RenderTier::DesktopMedium);
        CHECK(game::unlockResearch(p, "aspect.motion"));
        CHECK(game::unlockResearch(p, "aspect.form"));
        CHECK(!game::unlockResearch(p, "aspect.motion")); // already unlocked
        CHECK(!game::unlockResearch(p, ""));
        CHECK(game::hasResearch(p, "aspect.form"));
        CHECK(!game::hasResearch(p, "aspect.life"));
        CHECK_EQ(p.unlockedResearch.front(), std::string("aspect.form")); // kept sorted
        p.unlockedResearch.push_back("aspect.form");
        p.unlockedResearch.push_back("");
        CHECK(game::sanitize(p));
        CHECK_EQ(p.unlockedResearch.size(), std::size_t{2});
        CHECK(!game::sanitize(p));
    }
    return lt::test::finish("profile");
}

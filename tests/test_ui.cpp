// Section 17 UI model tests: main menu order, enable rules, focus navigation and activation; HUD
// meters, compass, visibility, optional damage numbers and the dp layout against the Section 22 touch
// controls; and Section 26 coverage of every UI loc key in en/ru. Every renderer (Windows, Linux,
// Android) draws from this same data.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "TestHarness.h"
#include "game/Ui.h"
#include "tcore/Input.h"
#include "tcore/Localization.h"

#ifndef LT_DATA_DIR
#define LT_DATA_DIR "data"
#endif

using namespace lt::game::ui;

static_assert(locKey(MenuEntry::NewJourney) == "menu.new_journey");
static_assert(locKey(MenuEntry::Exit) == "menu.exit");
static_assert(locKey(MenuEntry::Count).empty());
static_assert(locKey(Prompt::Deflect) == "prompt.deflect");
static_assert(locKey(Prompt::None).empty() && locKey(Prompt::Count).empty());

namespace {
MainMenu menuFor(bool hasCharacterSave, std::size_t worldSaves) {
    MenuContext ctx;
    ctx.hasCharacterSave = hasCharacterSave;
    ctx.worldSaves = worldSaves;
    return buildMainMenu(ctx);
}
bool enabled(const MainMenu& m, MenuEntry e) { return m.items[static_cast<std::size_t>(e)].enabled; }
}  // namespace

int main() {
    TEST_SECTION("menu order");
    MainMenu fresh = menuFor(false, 0);
    CHECK_EQ(fresh.items.size(), std::size_t{7});
    CHECK(fresh.studioKey == "menu.studio");
    for (std::size_t i = 0; i < fresh.items.size(); ++i) {
        CHECK(fresh.items[i].entry == static_cast<MenuEntry>(i));
        CHECK(fresh.items[i].key == kMenuKeys[i]);
        CHECK(fresh.items[i].key.substr(0, 5) == "menu.");
    }

    TEST_SECTION("menu enable rules");
    for (const bool save : {false, true})
        for (const std::size_t worlds : {std::size_t{0}, std::size_t{3}}) {
            const MainMenu m = menuFor(save, worlds);
            CHECK(enabled(m, MenuEntry::Continue) == save);
            CHECK(enabled(m, MenuEntry::LoadWorld) == (worlds > 0));
            CHECK(enabled(m, MenuEntry::NewJourney) && enabled(m, MenuEntry::Exit));
            CHECK(enabled(m, MenuEntry::Multiplayer) && enabled(m, MenuEntry::Settings));
            CHECK(enabled(m, MenuEntry::ModData));
            CHECK(m.focused().enabled);
            CHECK(m.activate() == (save ? MenuEntry::Continue : MenuEntry::NewJourney));
        }

    TEST_SECTION("menu navigation");
    CHECK(fresh.focused().entry == MenuEntry::NewJourney);
    fresh.move(1);  // Continue and Load World are disabled without saves
    CHECK(fresh.focused().entry == MenuEntry::Multiplayer);
    fresh.move(-1);
    CHECK(fresh.focused().entry == MenuEntry::NewJourney);
    fresh.move(-1);  // wraps to the bottom
    CHECK(fresh.focused().entry == MenuEntry::Exit);
    fresh.move(1);  // and back to the top
    CHECK(fresh.focused().entry == MenuEntry::NewJourney);
    fresh.move(0);
    CHECK(fresh.focused().entry == MenuEntry::NewJourney);
    fresh.move(5);  // only the sign of delta matters
    CHECK(fresh.focused().entry == MenuEntry::Multiplayer);
    CHECK(!fresh.select(MenuEntry::Continue));
    CHECK(fresh.focused().entry == MenuEntry::Multiplayer);
    CHECK(!fresh.select(MenuEntry::Count));
    CHECK(fresh.select(MenuEntry::ModData));
    CHECK(fresh.activate() == MenuEntry::ModData);
    CHECK(fresh.select(MenuEntry::Exit));
    CHECK(fresh.activate() == MenuEntry::Exit);
    MainMenu returning = menuFor(true, 0);
    returning.move(1);  // Load World stays disabled with no world file
    CHECK(returning.focused().entry == MenuEntry::Multiplayer);
    MainMenu full = menuFor(true, 2);
    full.move(1);
    CHECK(full.focused().entry == MenuEntry::LoadWorld);
    MainMenu empty;
    empty.move(1);
    CHECK(empty.activate() == MenuEntry::Count);

    TEST_SECTION("hud meters");
    const float kNaN = std::numeric_limits<float>::quiet_NaN();
    const float kInf = std::numeric_limits<float>::infinity();
    const auto frac = [](float current, float max) {
        Meter m;
        m.current = current;
        m.max = max;
        return m.fraction();
    };
    CHECK(frac(50.0f, 100.0f) == 0.5f);
    CHECK(frac(100.0f, 100.0f) == 1.0f);
    CHECK(frac(150.0f, 100.0f) == 1.0f);  // overheal never draws past the bar
    CHECK(frac(-5.0f, 100.0f) == 0.0f);
    CHECK(frac(5.0f, 0.0f) == 0.0f);
    CHECK(frac(5.0f, -10.0f) == 0.0f);
    CHECK(frac(kNaN, 100.0f) == 0.0f);
    CHECK(frac(10.0f, kNaN) == 0.0f);
    CHECK(frac(kInf, 100.0f) == 0.0f);
    CHECK(frac(10.0f, kInf) == 0.0f);
    CHECK(Meter{}.fraction() == 0.0f);

    TEST_SECTION("hud compass");
    CHECK(compassHeading(0.0f) == 0.0f);
    CHECK(compassHeading(90.0f) == 90.0f);
    CHECK(compassHeading(359.5f) == 359.5f);
    CHECK(compassHeading(360.0f) == 0.0f);
    CHECK(compassHeading(450.0f) == 90.0f);
    CHECK(compassHeading(-90.0f) == 270.0f);
    CHECK(compassHeading(-720.0f) == 0.0f);
    CHECK(compassHeading(-1e-6f) < 360.0f);  // -1e-6 + 360 rounds to 360 in float and must wrap to north
    CHECK(compassHeading(kNaN) == 0.0f);
    CHECK(compassHeading(kInf) == 0.0f && compassHeading(-kInf) == 0.0f);
    for (int k = -4000; k <= 4000; k += 7) {
        const float bearing = compassHeading(static_cast<float>(k) * 0.25f);
        CHECK(bearing >= 0.0f && bearing < 360.0f);
    }

    TEST_SECTION("hud visibility");
    HudState hud;
    for (const HudElement e : {HudElement::Health, HudElement::Stamina, HudElement::Vis, HudElement::Posture,
                               HudElement::Compass})
        CHECK(isVisible(hud, e));  // always on
    for (const HudElement e : {HudElement::QuickBelt, HudElement::ActiveResearch, HudElement::EventAlert,
                               HudElement::LockOn, HudElement::InteractionPrompt, HudElement::DamageNumbers,
                               HudElement::Count})
        CHECK(!isVisible(hud, e));  // hidden until relevant
    hud.quickBelt = {"flask.minor", ""};
    hud.activeResearch = "research.infusion";
    hud.eventNameKey = "event.flux_storm";
    hud.lockedOn = true;
    hud.prompt = Prompt::Scan;
    for (const HudElement e : {HudElement::QuickBelt, HudElement::ActiveResearch, HudElement::EventAlert,
                               HudElement::LockOn, HudElement::InteractionPrompt})
        CHECK(isVisible(hud, e));
    CHECK(!isVisible(hud, HudElement::DamageNumbers));
    hud.prompt = Prompt::Count;
    CHECK(!isVisible(hud, HudElement::InteractionPrompt));

    TEST_SECTION("hud damage numbers");
    HudState dmg;
    CHECK(!dmg.damageNumbersEnabled);  // optional and off by default
    dmg.addDamage(12.0f, 1.0f, 2.0f, 3.0f);
    CHECK(dmg.damageNumbers.empty());
    dmg.damageNumbersEnabled = true;
    CHECK(isVisible(dmg, HudElement::DamageNumbers));
    dmg.addDamage(12.0f, 1.0f, 2.0f, 3.0f);
    dmg.addDamage(kNaN, 0.0f, 0.0f, 0.0f);
    dmg.addDamage(kInf, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(dmg.damageNumbers.size(), std::size_t{1});
    CHECK(!dmg.damageNumbers.empty() && dmg.damageNumbers[0].amount == 12.0f && dmg.damageNumbers[0].z == 3.0f);
    CHECK(!dmg.damageNumbers.empty() && dmg.damageNumbers[0].age == 0.0f);
    dmg.tick(0.5f);
    dmg.addDamage(30.0f, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(dmg.damageNumbers.size(), std::size_t{2});
    dmg.tick(1.0f);  // the first hit passes its lifetime, the second does not
    CHECK_EQ(dmg.damageNumbers.size(), std::size_t{1});
    CHECK(!dmg.damageNumbers.empty() && dmg.damageNumbers[0].amount == 30.0f);
    dmg.tick(kNaN);  // a bad frame time drops numbers instead of pinning them on screen
    CHECK(dmg.damageNumbers.empty());
    dmg.addDamage(5.0f, 0.0f, 0.0f, 0.0f);
    dmg.damageNumbersEnabled = false;
    dmg.tick(0.0f);
    CHECK(dmg.damageNumbers.empty());

    TEST_SECTION("hud layout");
    namespace input = lt::input;
    const HudElement kPanels[] = {HudElement::Health,  HudElement::Stamina, HudElement::Vis,
                                  HudElement::Posture, HudElement::Compass, HudElement::ActiveResearch,
                                  HudElement::EventAlert, HudElement::QuickBelt};
    const auto inside = [](const input::Rect& r, float w, float h) {
        return r.w > 0.0f && r.h > 0.0f && r.x >= 0.0f && r.y >= 0.0f && r.x + r.w <= w && r.y + r.h <= h;
    };
    const float kScreens[][2] = {{640.0f, 360.0f}, {732.0f, 412.0f}, {915.0f, 412.0f}, {1280.0f, 720.0f},
                                 {1920.0f, 1080.0f}};
    for (const auto& screen : kScreens) {
        const float w = screen[0], h = screen[1];
        const HudLayout layout = buildHudLayout(w, h);
        for (std::size_t i = 0; i < kHudElementCount; ++i) CHECK(inside(layout.rects[i], w, h));
        // bars stack top-down on the left: health, stamina, Vis, then the research panel
        CHECK(layout[HudElement::Health].y + layout[HudElement::Health].h <= layout[HudElement::Stamina].y);
        CHECK(layout[HudElement::Stamina].y + layout[HudElement::Stamina].h <= layout[HudElement::Vis].y);
        CHECK(layout[HudElement::Vis].y + layout[HudElement::Vis].h <= layout[HudElement::ActiveResearch].y);
        CHECK(layout[HudElement::Posture].y + layout[HudElement::Posture].h <= layout[HudElement::Compass].y);
        CHECK(layout[HudElement::LockOn].x + layout[HudElement::LockOn].w * 0.5f == w * 0.5f);
        CHECK(layout[HudElement::LockOn].y + layout[HudElement::LockOn].h * 0.5f == h * 0.5f);
        CHECK(layout[HudElement::DamageNumbers].w == w && layout[HudElement::DamageNumbers].h == h);
        for (std::size_t i = 0; i < std::size(kPanels); ++i)
            for (std::size_t j = i + 1; j < std::size(kPanels); ++j)
                CHECK(!layout[kPanels[i]].intersects(layout[kPanels[j]]));
        // Section 22: the default touch controls never cover health/posture, nor any other panel
        const std::vector<input::TouchButton> touch = input::defaultTouchLayout(w, h);
        const auto issues = input::validateTouchLayout(touch, 1.0f, layout.protectedRects());
        for (const auto& issue : issues)
            std::printf("  touch issue action %d kind %d at %gx%g\n", static_cast<int>(issue.action),
                        static_cast<int>(issue.kind), w, h);
        CHECK(issues.empty());
        for (const input::TouchButton& b : touch)
            for (const HudElement e : kPanels) CHECK(!b.rectDp.intersects(layout[e]));
    }

    TEST_SECTION("hud protected rects");
    const HudLayout ref = buildHudLayout(800.0f, 400.0f);
    const std::vector<input::Rect> guarded = ref.protectedRects();
    const auto same = [](const input::Rect& a, const input::Rect& b) {
        return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
    };
    CHECK_EQ(guarded.size(), std::size_t{2});
    CHECK(guarded.size() == 2 && same(guarded[0], ref[HudElement::Health]) && same(guarded[1], ref[HudElement::Posture]));
    const auto hasIssue = [](const std::vector<input::TouchIssue>& v, input::Action a, input::TouchIssue::Kind k) {
        for (const auto& i : v)
            if (i.action == a && i.kind == k) return true;
        return false;
    };
    std::vector<input::TouchButton> dragged = input::defaultTouchLayout(800.0f, 400.0f);
    for (input::TouchButton& b : dragged) {
        if (b.action == input::Action::AttackLight) b.rectDp = {20.0f, 8.0f, 56.0f, 56.0f};    // onto health
        if (b.action == input::Action::Guard) b.rectDp = {390.0f, 0.0f, 56.0f, 56.0f};         // onto posture
    }
    const auto covered = input::validateTouchLayout(dragged, 1.0f, guarded);
    CHECK_EQ(covered.size(), std::size_t{2});
    CHECK(hasIssue(covered, input::Action::AttackLight, input::TouchIssue::CoversHud));
    CHECK(hasIssue(covered, input::Action::Guard, input::TouchIssue::CoversHud));
    for (input::TouchButton& b : dragged)
        if (b.action == input::Action::AttackLight || b.action == input::Action::Guard) b.visible = false;
    CHECK(input::validateTouchLayout(dragged, 1.0f, guarded).empty());  // hidden buttons cover nothing

    TEST_SECTION("settings menu rows");
    using Tier = lt::render::RenderTier;
    static_assert(kSettingsRowCount == 12);
    static_assert(locKey(SettingsRow::GraphicsTier) == "settings.graphics_tier");
    static_assert(locKey(SettingsRow::SaveRecovery) == "settings.save_recovery");
    static_assert(locKey(SettingsRow::Count).empty());
    const auto rowOn = [](const SettingsMenu& m, SettingsRow r) {
        return m.items[static_cast<std::size_t>(r)].enabled;
    };
    SettingsContext desk;
    desk.bestTier = Tier::DesktopUltra;
    const SettingsMenu ultra = buildSettingsMenu(lt::game::defaultSettings(Tier::DesktopUltra), desk);
    CHECK(ultra.titleKey == "menu.settings");
    CHECK_EQ(ultra.items.size(), kSettingsRowCount);
    for (std::size_t i = 0; i < ultra.items.size(); ++i) {
        const auto r = static_cast<SettingsRow>(i);
        CHECK(ultra.items[i].row == r);
        CHECK(ultra.items[i].key == kSettingsKeys[i] && ultra.items[i].key == locKey(r));
        CHECK(ultra.items[i].key.substr(0, 9) == "settings.");
        CHECK(pageFor(r) == (r == SettingsRow::TouchLayout    ? SettingsPage::TouchLayout
                             : r == SettingsRow::AudioMix     ? SettingsPage::AudioMix
                             : r == SettingsRow::SaveRecovery ? SettingsPage::SaveRecovery
                                                              : SettingsPage::None));
    }
    CHECK(pageFor(SettingsRow::Count) == SettingsPage::None);

    TEST_SECTION("settings enable rules");
    const std::vector<Tier> ultraTiers = selectableTiers(Tier::DesktopUltra);
    CHECK_EQ(ultraTiers.size(), std::size_t{2});
    CHECK(ultraTiers.size() == 2 && ultraTiers[0] == Tier::DesktopUltra && ultraTiers[1] == Tier::DesktopMedium);
    for (const Tier t : {Tier::DesktopMedium, Tier::AndroidC31}) {  // never above what the GPU reported
        const std::vector<Tier> only = selectableTiers(t);
        CHECK(only.size() == 1 && only[0] == t);
    }
    CHECK(rowOn(ultra, SettingsRow::GraphicsTier));         // Ultra may step down to Medium
    CHECK(!rowOn(ultra, SettingsRow::InternalResolution));  // desktop tiers render at a fixed 1080p
    CHECK(!rowOn(ultra, SettingsRow::TouchLayout));         // no touch screen
    CHECK(!rowOn(ultra, SettingsRow::SaveRecovery));        // no backup to recover from
    for (const SettingsRow r : {SettingsRow::CameraDistance, SettingsRow::MotionReduction, SettingsRow::Colourblind,
                                SettingsRow::Subtitles, SettingsRow::FontScale, SettingsRow::ControllerLayout,
                                SettingsRow::Vibration, SettingsRow::AudioMix})
        CHECK(rowOn(ultra, r));
    CHECK(ultra.focused().row == SettingsRow::GraphicsTier);
    CHECK(!rowEnabled(SettingsRow::Count, lt::game::Settings{}, desk));
    SettingsContext phone;
    phone.bestTier = Tier::AndroidC31;
    phone.touchDevice = true;
    phone.recoverableBackups = 2;
    SettingsMenu phoneMenu = buildSettingsMenu(lt::game::defaultSettings(Tier::AndroidC31), phone);
    CHECK(!rowOn(phoneMenu, SettingsRow::GraphicsTier));       // Android stays on its GLES tier
    CHECK(rowOn(phoneMenu, SettingsRow::InternalResolution));  // the C31 tier scales 512..720 px
    CHECK(rowOn(phoneMenu, SettingsRow::TouchLayout));
    CHECK(rowOn(phoneMenu, SettingsRow::SaveRecovery));
    CHECK(phoneMenu.focused().row == SettingsRow::InternalResolution);  // focus skips the disabled first row
    SettingsContext medium;  // defaults: DesktopMedium GPU, no touch screen, no backups
    SettingsMenu med = buildSettingsMenu(lt::game::defaultSettings(Tier::DesktopMedium), medium);
    CHECK(!rowOn(med, SettingsRow::GraphicsTier) && !rowOn(med, SettingsRow::InternalResolution));
    CHECK(med.focused().row == SettingsRow::CameraDistance);  // Camera Distance is never disabled

    TEST_SECTION("settings navigation");
    CHECK(med.activate() == SettingsPage::None);  // value rows open no page
    med.move(1);
    CHECK(med.focused().row == SettingsRow::MotionReduction);
    med.move(-1);
    CHECK(med.focused().row == SettingsRow::CameraDistance);
    med.move(-1);  // wraps back past Internal Resolution, Graphics Tier and Save Recovery
    CHECK(med.focused().row == SettingsRow::AudioMix);
    CHECK(med.activate() == SettingsPage::AudioMix);
    med.move(1);  // and forward past the same disabled rows
    CHECK(med.focused().row == SettingsRow::CameraDistance);
    med.move(0);
    CHECK(med.focused().row == SettingsRow::CameraDistance);
    med.move(-3);  // only the sign of delta matters
    CHECK(med.focused().row == SettingsRow::AudioMix);
    med.focus = static_cast<std::size_t>(SettingsRow::ControllerLayout);
    med.move(1);  // no touch screen: Touch Layout is skipped
    CHECK(med.focused().row == SettingsRow::Vibration);
    med.focus = static_cast<std::size_t>(SettingsRow::SaveRecovery);  // forced onto a disabled row
    CHECK(med.activate() == SettingsPage::None);
    for (int i = 0; i < 7; ++i) phoneMenu.move(1);
    CHECK(phoneMenu.focused().row == SettingsRow::TouchLayout);
    CHECK(phoneMenu.activate() == SettingsPage::TouchLayout);
    phoneMenu.move(1);
    CHECK(phoneMenu.focused().row == SettingsRow::Vibration);
    CHECK(phoneMenu.activate() == SettingsPage::None);
    phoneMenu.move(1);
    CHECK(phoneMenu.activate() == SettingsPage::AudioMix);
    phoneMenu.move(1);
    CHECK(phoneMenu.focused().row == SettingsRow::SaveRecovery);
    CHECK(phoneMenu.activate() == SettingsPage::SaveRecovery);
    phoneMenu.move(1);  // wraps past the single-tier Graphics Tier row
    CHECK(phoneMenu.focused().row == SettingsRow::InternalResolution);
    SettingsMenu noRows;
    noRows.move(1);
    CHECK(noRows.activate() == SettingsPage::None);

    TEST_SECTION("settings value steps");
    using lt::game::ColourblindPalette;
    using lt::game::ControllerLayout;
    using lt::game::SubtitleMode;
    static_assert(settings_detail::cycle(0, 4, 1) == 1 && settings_detail::cycle(0, 4, -1) == 3);
    static_assert(settings_detail::cycle(3, 4, 1) == 0 && settings_detail::cycle(5, 4, 1) == 2);
    static_assert(settings_detail::cycle(2, 0, 1) == 0);
    CHECK(resolutionSteps(Tier::AndroidC31) == std::vector<int>({512, 576, 640, 720}));  // 704 folds into 720
    CHECK(resolutionSteps(Tier::DesktopUltra) == std::vector<int>({1080}));
    CHECK(resolutionSteps(Tier::DesktopMedium) == std::vector<int>({1080}));
    for (const Tier t : {Tier::DesktopUltra, Tier::DesktopMedium, Tier::AndroidC31}) {
        const std::vector<int> steps = resolutionSteps(t);
        const lt::render::TierFeatures f = lt::render::featuresFor(t);
        CHECK(!steps.empty() && steps.front() == f.internalResMin && steps.back() == f.internalResMax);
        for (std::size_t i = 1; i < steps.size(); ++i) CHECK(steps[i] > steps[i - 1]);
        bool hasDefault = false;
        for (const int r : steps) hasDefault = hasDefault || r == f.internalResDefault;
        CHECK(hasDefault);  // the platform default is always one of the choices
    }

    TEST_SECTION("settings value edits");
    {
        lt::game::Settings s = lt::game::defaultSettings(Tier::DesktopMedium);
        // camera distance: 0.5 m steps that stop at 2..8 m; off-grid values snap to the next line
        CHECK(adjustSetting(s, SettingsRow::CameraDistance, 1, medium) && s.cameraDistance == 4.5f);
        CHECK(adjustSetting(s, SettingsRow::CameraDistance, -1, medium) && s.cameraDistance == 4.0f);
        CHECK(adjustSetting(s, SettingsRow::CameraDistance, -1, medium) && s.cameraDistance == 3.5f);
        s.cameraDistance = lt::game::kCameraDistanceMax;
        CHECK(!adjustSetting(s, SettingsRow::CameraDistance, 1, medium));
        CHECK(s.cameraDistance == lt::game::kCameraDistanceMax);
        s.cameraDistance = lt::game::kCameraDistanceMin;
        CHECK(!adjustSetting(s, SettingsRow::CameraDistance, -1, medium));
        CHECK(s.cameraDistance == lt::game::kCameraDistanceMin);
        s.cameraDistance = 4.3f;
        CHECK(adjustSetting(s, SettingsRow::CameraDistance, 1, medium) && s.cameraDistance == 4.5f);
        s.cameraDistance = 4.3f;
        CHECK(adjustSetting(s, SettingsRow::CameraDistance, -1, medium) && s.cameraDistance == 4.0f);
        // font size: 0.25 steps inside 0.75..2.0
        CHECK(adjustSetting(s, SettingsRow::FontScale, 1, medium) && s.fontScale == 1.25f);
        CHECK(adjustSetting(s, SettingsRow::FontScale, -1, medium) && s.fontScale == 1.0f);
        CHECK(adjustSetting(s, SettingsRow::FontScale, -1, medium) && s.fontScale == 0.75f);
        CHECK(!adjustSetting(s, SettingsRow::FontScale, -1, medium) && s.fontScale == lt::game::kFontScaleMin);
        s.fontScale = lt::game::kFontScaleMax;
        CHECK(!adjustSetting(s, SettingsRow::FontScale, 1, medium) && s.fontScale == lt::game::kFontScaleMax);
        // toggles flip on either direction
        CHECK(adjustSetting(s, SettingsRow::MotionReduction, 1, medium) && s.motionReduction);
        CHECK(adjustSetting(s, SettingsRow::MotionReduction, -1, medium) && !s.motionReduction);
        CHECK(adjustSetting(s, SettingsRow::Vibration, -1, medium) && !s.vibration);
        CHECK(adjustSetting(s, SettingsRow::Vibration, 1, medium) && s.vibration);
        // lists wrap at both ends
        CHECK(adjustSetting(s, SettingsRow::Colourblind, 1, medium) && s.colourblind == ColourblindPalette::Protanopia);
        CHECK(adjustSetting(s, SettingsRow::Colourblind, -1, medium) && s.colourblind == ColourblindPalette::Off);
        CHECK(adjustSetting(s, SettingsRow::Colourblind, -1, medium) &&
              s.colourblind == ColourblindPalette::Tritanopia);
        CHECK(adjustSetting(s, SettingsRow::Colourblind, 1, medium) && s.colourblind == ColourblindPalette::Off);
        for (int i = 0; i < 4; ++i) adjustSetting(s, SettingsRow::Colourblind, 1, medium);  // one full lap
        CHECK(s.colourblind == ColourblindPalette::Off);
        CHECK(adjustSetting(s, SettingsRow::Subtitles, 1, medium) && s.subtitles == SubtitleMode::DialogueAndCues);
        CHECK(adjustSetting(s, SettingsRow::Subtitles, 1, medium) && s.subtitles == SubtitleMode::Off);
        CHECK(adjustSetting(s, SettingsRow::Subtitles, -1, medium) && s.subtitles == SubtitleMode::DialogueAndCues);
        CHECK(adjustSetting(s, SettingsRow::Subtitles, -1, medium) && s.subtitles == SubtitleMode::Dialogue);
        CHECK(adjustSetting(s, SettingsRow::ControllerLayout, -1, medium) &&
              s.controllerLayout == ControllerLayout::Legacy);
        CHECK(adjustSetting(s, SettingsRow::ControllerLayout, 1, medium) &&
              s.controllerLayout == ControllerLayout::Standard);
        CHECK(adjustSetting(s, SettingsRow::ControllerLayout, 1, medium) &&
              s.controllerLayout == ControllerLayout::Southpaw);
        // page rows, disabled rows (single tier, fixed 1080p), dir == 0 and Count change nothing
        const lt::game::Settings before = s;
        for (const SettingsRow r : {SettingsRow::TouchLayout, SettingsRow::AudioMix, SettingsRow::SaveRecovery,
                                    SettingsRow::GraphicsTier, SettingsRow::InternalResolution, SettingsRow::Count})
            CHECK(!adjustSetting(s, r, 1, medium) && !adjustSetting(s, r, -1, medium));
        CHECK(!adjustSetting(s, SettingsRow::CameraDistance, 0, medium));
        CHECK(s == before);
    }

    TEST_SECTION("audio mix page");
    static_assert(kAudioBusCount == 5);
    static_assert(locKey(AudioBus::Master) == "audio.master");
    static_assert(locKey(AudioBus::Ambience) == "audio.ambience");
    static_assert(locKey(AudioBus::Count).empty());
    {
        using lt::game::AudioMix;
        AudioMixPage page;
        CHECK(page.titleKey == locKey(SettingsRow::AudioMix));  // titled like the row that opens it
        CHECK(page.focus == AudioBus::Master);
        const AudioMix defaults{};
        const auto rows = page.sliders(defaults);
        const int kDefaultPercents[kAudioBusCount] = {100, 80, 100, 100, 80};
        for (std::size_t i = 0; i < kAudioBusCount; ++i) {
            CHECK(rows[i].bus == static_cast<AudioBus>(i));
            CHECK(rows[i].key == kAudioBusKeys[i] && rows[i].key == locKey(rows[i].bus));
            CHECK(rows[i].key.substr(0, 6) == "audio.");
            CHECK_EQ(rows[i].percent, kDefaultPercents[i]);
        }

        // up/down wraps at both ends and only the sign of delta matters
        page.move(1);
        CHECK(page.focus == AudioBus::Music);
        page.move(-1);
        CHECK(page.focus == AudioBus::Master);
        page.move(-1);
        CHECK(page.focus == AudioBus::Ambience);
        page.move(1);
        CHECK(page.focus == AudioBus::Master);
        page.move(0);
        CHECK(page.focus == AudioBus::Master);
        page.move(4);
        CHECK(page.focus == AudioBus::Music);

        // percent labels show what sanitize() stores
        CHECK(gainPercent(0.0f) == 0 && gainPercent(0.25f) == 25 && gainPercent(0.8f) == 80);
        CHECK(gainPercent(1.0f) == 100);
        CHECK(gainPercent(-0.5f) == 0 && gainPercent(1.5f) == 100);
        CHECK(gainPercent(kNaN) == 100 && gainPercent(kInf) == 100 && gainPercent(-kInf) == 100);
        lt::game::Settings corrupt = lt::game::defaultSettings(Tier::DesktopMedium);
        corrupt.audio.voice = kNaN;
        corrupt.audio.music = 3.0f;
        corrupt.audio.ambience = -1.0f;
        CHECK(lt::game::sanitize(corrupt));
        CHECK(gainPercent(corrupt.audio.voice) == gainPercent(kNaN));
        CHECK(gainPercent(corrupt.audio.music) == gainPercent(3.0f));
        CHECK(gainPercent(corrupt.audio.ambience) == gainPercent(-1.0f));

        // left/right steps 10% and stops at mute and full
        AudioMix mix{};
        CHECK(!adjustGain(mix, AudioBus::Master, 1) && mix.master == 1.0f);
        CHECK(adjustGain(mix, AudioBus::Music, 1) && gainPercent(mix.music) == 90);
        CHECK(adjustGain(mix, AudioBus::Music, 1) && mix.music == 1.0f);
        CHECK(!adjustGain(mix, AudioBus::Music, 1) && mix.music == 1.0f);
        for (int i = 0; i < 8; ++i) CHECK(adjustGain(mix, AudioBus::Ambience, -1));
        CHECK(mix.ambience == 0.0f);
        CHECK(!adjustGain(mix, AudioBus::Ambience, -1) && mix.ambience == 0.0f);
        for (int i = 0; i < 10; ++i) {
            CHECK(adjustGain(mix, AudioBus::Ambience, 1));
            CHECK_EQ(gainPercent(mix.ambience), (i + 1) * 10);
        }
        CHECK(mix.ambience == 1.0f && !adjustGain(mix, AudioBus::Ambience, 1));
        mix.voice = 0.55f;  // an off-grid gain snaps to the neighbouring 10% mark
        CHECK(adjustGain(mix, AudioBus::Voice, 1) && gainPercent(mix.voice) == 60);
        mix.voice = 0.55f;
        CHECK(adjustGain(mix, AudioBus::Voice, -1) && gainPercent(mix.voice) == 50);
        mix.effects = kNaN;  // a corrupt gain steps from full volume, as sanitize() stores it
        CHECK(adjustGain(mix, AudioBus::Effects, -1) && gainPercent(mix.effects) == 90);
        mix.effects = kNaN;
        CHECK(adjustGain(mix, AudioBus::Effects, 1) && mix.effects == 1.0f);
        mix.effects = 1.7f;
        CHECK(adjustGain(mix, AudioBus::Effects, 1) && mix.effects == 1.0f);
        mix.effects = -0.2f;
        CHECK(adjustGain(mix, AudioBus::Effects, -1) && mix.effects == 0.0f);
        const AudioMix untouched = mix;
        CHECK(!adjustGain(mix, AudioBus::Voice, 0));
        CHECK(!adjustGain(mix, AudioBus::Count, 1) && !adjustGain(mix, AudioBus::Count, -1));
        CHECK(mix == untouched);
        CHECK(gainOf(mix, AudioBus::Count) == 0.0f);
    }

    TEST_SECTION("touch layout page");
    static_assert(kTouchControlCount == 10 && kTouchRowCount == 12 && kTouchResetRow == 11);
    static_assert(locKey(TouchEdit::TooSmall) == "touch.too_small" && locKey(TouchEdit::CoversHud) == "touch.covers_hud");
    static_assert(locKey(TouchEdit::None).empty() && locKey(TouchEdit::Changed).empty());
    static_assert(TouchLayoutPage::actionAt(0) == input::Action::Count && TouchLayoutPage::actionAt(1) == input::Action::Move);
    static_assert(TouchLayoutPage::actionAt(10) == input::Action::LockOn);
    static_assert(TouchLayoutPage::actionAt(kTouchResetRow) == input::Action::Count);
    {
        using lt::game::TouchLayoutPrefs;
        const lt::game::Settings stock = lt::game::defaultSettings(Tier::AndroidC31);
        const TouchLayoutPrefs stockLayout{};
        TouchLayoutPage page;
        CHECK(page.titleKey == locKey(SettingsRow::TouchLayout));  // titled like the row that opens it
        CHECK(page.focus == 0 && page.last == TouchEdit::None);
        const auto rows = page.rows(stock, stockLayout);
        CHECK(rows[0].kind == TouchRowKind::GlobalSize && rows[0].key == kTouchSizeKey && rows[0].percent == 100);
        for (std::size_t i = 0; i < kTouchControlCount; ++i) {
            const TouchRow& r = rows[i + 1];
            CHECK(r.kind == TouchRowKind::Control && r.action == lt::game::kTouchActions[i]);
            CHECK(r.action == TouchLayoutPage::actionAt(i + 1));
            CHECK(r.key == kTouchControlKeys[i] && r.key.substr(0, 6) == "touch.");
            CHECK(r.percent == 100 && r.visible);
            CHECK(r.hideable == (i >= 2));  // Move and Look are regions, never hidden
        }
        CHECK(rows[kTouchResetRow].kind == TouchRowKind::Reset && rows[kTouchResetRow].key == kTouchResetKey);

        // up/down wraps at both ends and only the sign of delta matters
        page.move(-1);
        CHECK(page.focus == kTouchResetRow);
        page.move(1);
        CHECK(page.focus == 0);
        page.move(0);
        CHECK(page.focus == 0);
        page.move(5);
        CHECK(page.focus == 1);

        // percent labels show what sanitize() stores
        CHECK(touchPercent(0.5f) == 50 && touchPercent(1.0f) == 100 && touchPercent(2.0f) == 200);
        CHECK(touchPercent(0.1f) == 50 && touchPercent(3.0f) == 200);
        CHECK(touchPercent(kNaN) == 100 && touchPercent(kInf) == 100 && touchPercent(-kInf) == 100);
        TouchLayoutPrefs corrupt;
        corrupt.buttons[0] = {kNaN, false};  // Move hidden by a hand-edit
        corrupt.buttons[4] = {5.0f, false};
        lt::game::Settings corruptSettings = stock;
        corruptSettings.touchScale = kNaN;
        const auto shown = page.rows(corruptSettings, corrupt);
        CHECK(shown[0].percent == 100 && shown[1].percent == 100 && shown[1].visible);
        CHECK(shown[5].percent == 200 && !shown[5].visible);

        // global size steps 10% on a 915x412dp phone until the camera pad would cover the posture bar
        const float W = 915.0f, H = 412.0f;
        lt::game::Settings s = stock;
        TouchLayoutPrefs t;
        page.focus = 0;
        for (int i = 1; i <= 5; ++i) {
            CHECK(adjustFocused(page, s, t, 1, W, H) == TouchEdit::Changed);
            CHECK_EQ(touchPercent(s.touchScale), 100 + i * 10);
        }
        CHECK(adjustFocused(page, s, t, 1, W, H) == TouchEdit::CoversHud);
        CHECK(page.last == TouchEdit::CoversHud && touchPercent(s.touchScale) == 150 && t == stockLayout);
        CHECK(locKey(page.last) == "touch.covers_hud");
        for (int i = 0; i < 10; ++i) CHECK(adjustFocused(page, s, t, -1, W, H) == TouchEdit::Changed);
        CHECK(touchPercent(s.touchScale) == 50 && page.last == TouchEdit::Changed);
        CHECK(adjustFocused(page, s, t, -1, W, H) == TouchEdit::None && s.touchScale == 0.5f);
        CHECK(page.last == TouchEdit::None);
        CHECK(adjustFocused(page, s, t, 0, W, H) == TouchEdit::None);
        s.touchScale = kNaN;  // a corrupt size steps from what sanitize() stores
        CHECK(adjustFocused(page, s, t, 1, W, H) == TouchEdit::Changed && touchPercent(s.touchScale) == 110);
        s.touchScale = 1.0f;

        // per-control size: the action cluster grows to 200%, Look stops before the posture bar
        page.focus = 3;  // Attack
        CHECK(TouchLayoutPage::actionAt(page.focus) == input::Action::AttackLight);
        for (int i = 0; i < 10; ++i) CHECK(adjustFocused(page, s, t, 1, W, H) == TouchEdit::Changed);
        CHECK(adjustFocused(page, s, t, 1, W, H) == TouchEdit::None);
        CHECK(page.rows(s, t)[3].percent == 200 && s.touchScale == 1.0f);
        page.focus = 2;  // Look
        for (int i = 0; i < 5; ++i) CHECK(adjustFocused(page, s, t, 1, W, H) == TouchEdit::Changed);
        CHECK(adjustFocused(page, s, t, 1, W, H) == TouchEdit::CoversHud && page.rows(s, t)[2].percent == 150);
        t.buttons[3].scale = kNaN;  // Guard: a corrupt size steps from full size
        page.focus = 4;
        CHECK(adjustFocused(page, s, t, -1, W, H) == TouchEdit::Changed && page.rows(s, t)[4].percent == 90);

        // confirm hides and shows hideable controls; size rows, Move and Look ignore it
        for (const std::size_t row : {std::size_t{0}, std::size_t{1}, std::size_t{2}}) {
            page.focus = row;
            const auto before = t;
            CHECK(confirmFocused(page, s, t, W, H) == TouchEdit::None && t == before);
        }
        page.focus = 3;
        CHECK(confirmFocused(page, s, t, W, H) == TouchEdit::Changed && !page.rows(s, t)[3].visible);
        CHECK(confirmFocused(page, s, t, W, H) == TouchEdit::Changed && page.rows(s, t)[3].visible);
        page.focus = kTouchResetRow;
        CHECK(adjustFocused(page, s, t, 1, W, H) == TouchEdit::None);  // Reset has no size

        // Reset restores 100% and every control shown, and is a no-op on the defaults
        CHECK(confirmFocused(page, s, t, W, H) == TouchEdit::Changed);
        CHECK(s.touchScale == 1.0f && t == stockLayout && page.last == TouchEdit::Changed);
        CHECK(confirmFocused(page, s, t, W, H) == TouchEdit::None && page.last == TouchEdit::None);
    }

    TEST_SECTION("save recovery page");
    static_assert(locKey(lt::core::SaveLayer::Profile) == "save.profile");
    static_assert(locKey(lt::core::SaveLayer::CoopLedger) == "save.coop_ledger");
    static_assert(locKey(static_cast<lt::core::SaveLayer>(0)).empty() && locKey(static_cast<lt::core::SaveLayer>(6)).empty());
    static_assert(locKey(SaveState::Good) == "save.good" && locKey(SaveState::Recoverable) == "save.recoverable");
    static_assert(locKey(SaveState::Lost) == "save.lost");
    static_assert(locKey(SaveRestore::None).empty() && locKey(SaveRestore::Restored) == "save.restored");
    static_assert(locKey(SaveRestore::Failed) == "save.restore_failed");
    {
        namespace fs = std::filesystem;
        using lt::core::SaveFile;
        using lt::core::SaveLayer;
        const fs::path dir = fs::temp_directory_path() / "tm_test_ui_saves";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        // n atomic writes: the newest is the main file, older ones rotate to .bak1/.bak2; payload[1]
        // records which write a copy came from.
        const auto writeSave = [](const fs::path& p, SaveLayer layer, int n, std::uint32_t version) {
            bool ok = true;
            for (int i = 1; i <= n; ++i) {
                lt::core::SaveEnvelope e;
                e.layer = layer;
                e.schemaVersion = version;
                e.payload = {static_cast<std::uint8_t>(layer), static_cast<std::uint8_t>(i), 0x5A};
                ok = SaveFile::writeAtomic(p, e) && ok;
            }
            return ok;
        };
        // Flips the first payload byte on disk so the checksum no longer matches.
        const auto damage = [](const fs::path& p) {
            std::string bytes;
            {
                std::ifstream in(p, std::ios::binary);
                std::ostringstream ss;
                ss << in.rdbuf();
                bytes = ss.str();
            }
            if (bytes.size() <= lt::tools::kSaveHeaderBytes) return false;
            bytes[lt::tools::kSaveHeaderBytes] = static_cast<char>(bytes[lt::tools::kSaveHeaderBytes] ^ 0xFF);
            std::ofstream out(p, std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            return static_cast<bool>(out);
        };
        const auto bak = [](const fs::path& p, int i) { return lt::tools::saveChainPath(p, i); };

        const fs::path profile = dir / "profile.json", character = dir / "character.tmc";
        const fs::path world = dir / "world_1234.tmw", journal = dir / "journal_aurelia.tmj";
        const fs::path ledger = dir / "coop_ledger.tml";
        CHECK(writeSave(profile, SaveLayer::Profile, 3, 1));        // intact, two good backups
        CHECK(writeSave(character, SaveLayer::Character, 2, 1));    // main damaged -> .bak1
        CHECK(damage(character));
        CHECK(writeSave(world, SaveLayer::WorldSeed, 3, 1));        // main and .bak1 damaged -> .bak2
        CHECK(damage(world) && damage(bak(world, 1)));
        { std::ofstream(SaveFile::withSuffix(world, ".tmp"), std::ios::binary) << "torn"; }  // interrupted write
        CHECK(writeSave(journal, SaveLayer::WorldJournal, 1, 1));   // main damaged, no backup
        CHECK(damage(journal));
        CHECK(writeSave(ledger, SaveLayer::CoopLedger, 1, 1));      // v2 main from a newer build -> v1 .bak1
        CHECK(writeSave(ledger, SaveLayer::CoopLedger, 1, 2));
        const std::vector<SaveSlot> slots{{SaveLayer::Profile, profile, 1},
                                          {SaveLayer::Character, character, 1},
                                          {SaveLayer::WorldSeed, world, 1},
                                          {SaveLayer::WorldJournal, journal, 1},
                                          {SaveLayer::CoopLedger, ledger, 1}};

        // the settings row is enabled by the valid backups: 2 + 1 + 1 + 0 + 1
        CHECK_EQ(countRecoverableBackups(slots), std::size_t{5});
        CHECK_EQ(countRecoverableBackups({}), std::size_t{0});

        SaveRecoveryPage page = buildSaveRecoveryPage(slots);
        CHECK(page.titleKey == locKey(SettingsRow::SaveRecovery));  // titled like the row that opens it
        CHECK(page.rows.size() == slots.size() && page.focus == 0);
        CHECK(page.last == SaveRestore::None && page.lastBackup == 0 && page.anyRecoverable());
        for (std::size_t i = 0; i < page.rows.size(); ++i) {
            const SaveRecoveryRow& r = page.rows[i];
            CHECK(r.layer == slots[i].layer && r.key == kSaveLayerKeys[i] && r.stateKey == locKey(r.state));
        }
        CHECK(page.rows[0].state == SaveState::Good && page.rows[0].backup == 0 && !page.rows[0].interruptedWrite);
        CHECK(page.rows[1].state == SaveState::Recoverable && page.rows[1].backup == 1 && !page.rows[1].interruptedWrite);
        CHECK(page.rows[2].state == SaveState::Recoverable && page.rows[2].backup == 2 && page.rows[2].interruptedWrite);
        CHECK(page.rows[3].state == SaveState::Lost && page.rows[3].backup == 0);
        CHECK(page.rows[4].state == SaveState::Recoverable && page.rows[4].backup == 1);  // newer schema
        CHECK(page.rows[3].stateKey == "save.lost" && page.rows[4].stateKey == "save.recoverable");

        // up/down wraps at both ends and only the sign of delta matters
        page.move(-1);
        CHECK(page.focus == 4);
        page.move(1);
        CHECK(page.focus == 0);
        page.move(0);
        CHECK(page.focus == 0);
        page.move(3);
        CHECK(page.focus == 1);

        fs::remove_all(dir, ec);
    }

    TEST_SECTION("ui loc keys");
    const auto readFile = [](const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    const std::string dir = LT_DATA_DIR;
    lt::loc::StringTable en(lt::loc::kSourceLocale), ru(lt::loc::kFirstTargetLocale);
    std::string err;
    CHECK(en.parse(readFile(dir + "/loc/en.txt"), &err));
    CHECK(ru.parse(readFile(dir + "/loc/ru.txt"), &err));
    if (!err.empty()) std::printf("  loc parse error: %s\n", err.c_str());
    std::vector<std::string_view> uiKeys(kMenuKeys.begin(), kMenuKeys.end());
    uiKeys.push_back(kStudioKey);
    for (const std::string_view k : kPromptKeys)
        if (!k.empty()) uiKeys.push_back(k);
    uiKeys.push_back(kEventAlertKey);
    uiKeys.push_back(kLockOnKey);
    CHECK_EQ(uiKeys.size(), std::size_t{16});
    for (const std::string_view k : uiKeys) {
        const std::string key(k);
        CHECK(lt::loc::StringTable::validKey(key));
        for (const lt::loc::StringTable* table : {&en, &ru}) {
            const std::string* text = table->find(key);
            if (text == nullptr || text->empty()) std::printf("  %s missing %s\n", table->locale().c_str(), key.c_str());
            CHECK(text != nullptr && !text->empty());
        }
    }
    for (const lt::loc::StringTable* table : {&en, &ru}) {  // "Event: {0}" takes the event name
        const std::string* alert = table->find(std::string(kEventAlertKey));
        CHECK(alert != nullptr && alert->find("{0}") != std::string::npos);
    }

    return ::lt::test::finish("ui");
}

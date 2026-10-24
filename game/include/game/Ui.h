#pragma once
// Section 17 UI model, platform-neutral: the main menu and HUD are data (localisation keys, enable
// rules, focus, dp rects) that each renderer draws, so Windows, Linux and Android share one
// behaviour and no player-facing text is embedded in C++ (Section 26).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "game/Profile.h"
#include "tcore/Input.h"
#include "tcore/SaveFile.h"
#include "trender/RenderTier.h"
#include "ttools/SaveInspector.h"

namespace lt::game::ui {

// ---- Main menu (Section 17) ----

enum class MenuEntry : std::uint8_t {
    NewJourney = 0, Continue, LoadWorld, Multiplayer, Settings, ModData, Exit, Count
};

inline constexpr std::array<std::string_view, static_cast<std::size_t>(MenuEntry::Count)> kMenuKeys{
    "menu.new_journey", "menu.continue", "menu.load_world", "menu.multiplayer",
    "menu.settings",    "menu.mod_data", "menu.exit"};
// Caption under the RDGaming Studios logo shown above the entries (Sections 17, 20).
inline constexpr std::string_view kStudioKey = "menu.studio";

constexpr std::string_view locKey(MenuEntry e) noexcept {
    return e < MenuEntry::Count ? kMenuKeys[static_cast<std::size_t>(e)] : std::string_view{};
}

// What the menu needs to know about the install, gathered from saves/ at boot.
struct MenuContext {
    bool hasCharacterSave = false; // Continue resumes the most recent character
    std::size_t worldSaves = 0;    // world_<seed>.tmw files Load World can list
};

struct MenuItem {
    MenuEntry entry = MenuEntry::NewJourney;
    std::string_view key;
    bool enabled = true;
};

struct MainMenu {
    std::string_view studioKey = kStudioKey;
    std::vector<MenuItem> items; // all seven entries in Section 17 order; disabled ones stay listed
    std::size_t focus = 0;       // index into items; always an enabled entry

    const MenuItem& focused() const { return items[focus]; }

    // Controller/keyboard navigation: steps focus by the sign of delta, wrapping and skipping
    // disabled entries. New Journey and Exit are never disabled, so an enabled item is always found.
    void move(int delta) noexcept {
        if (items.empty() || delta == 0) return;
        const std::size_t n = items.size();
        const std::size_t step = delta > 0 ? 1 : n - 1;
        std::size_t i = focus;
        do { i = (i + step) % n; } while (!items[i].enabled && i != focus);
        focus = i;
    }

    // Pointer/touch: focuses an entry directly; refuses disabled entries.
    bool select(MenuEntry e) noexcept {
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].entry == e) {
                if (!items[i].enabled) return false;
                focus = i;
                return true;
            }
        return false;
    }

    // Confirms the focused entry; MenuEntry::Count means nothing happens.
    MenuEntry activate() const noexcept {
        return items.empty() || !items[focus].enabled ? MenuEntry::Count : items[focus].entry;
    }
};

// Continue needs a character save and Load World needs a world file. Multiplayer stays enabled
// offline because hosting co-op never depends on the matchmaking service (Section 13).
inline MainMenu buildMainMenu(const MenuContext& ctx) {
    MainMenu m;
    m.items.reserve(kMenuKeys.size());
    for (std::size_t i = 0; i < kMenuKeys.size(); ++i) {
        const auto e = static_cast<MenuEntry>(i);
        bool enabled = true;
        if (e == MenuEntry::Continue) enabled = ctx.hasCharacterSave;
        else if (e == MenuEntry::LoadWorld) enabled = ctx.worldSaves > 0;
        m.items.push_back(MenuItem{e, kMenuKeys[i], enabled});
    }
    // Returning players land on Continue; a fresh install lands on New Journey.
    m.focus = ctx.hasCharacterSave ? static_cast<std::size_t>(MenuEntry::Continue) : 0;
    return m;
}

// ---- HUD (Section 17) ----

enum class HudElement : std::uint8_t {
    Health = 0, Stamina, Vis, Posture, QuickBelt, ActiveResearch, Compass, EventAlert, LockOn,
    InteractionPrompt, DamageNumbers, Count
};
inline constexpr std::size_t kHudElementCount = static_cast<std::size_t>(HudElement::Count);
inline constexpr std::string_view kEventAlertKey = "hud.event_alert"; // "Event: {0}", arg = event name key
inline constexpr std::string_view kLockOnKey = "hud.lock_on";

// Interaction prompts use the short Section 26 labels so they stay readable on a 6-inch screen.
enum class Prompt : std::uint8_t { None = 0, Guard, Deflect, Scan, Infuse, Rest, Build, Count };
inline constexpr std::array<std::string_view, static_cast<std::size_t>(Prompt::Count)> kPromptKeys{
    "", "prompt.guard", "prompt.deflect", "prompt.scan", "prompt.infuse", "prompt.rest", "prompt.build"};

constexpr std::string_view locKey(Prompt p) noexcept {
    return p < Prompt::Count ? kPromptKeys[static_cast<std::size_t>(p)] : std::string_view{};
}

// A resource bar. fraction() is clamped and NaN-safe, so a bad value never draws outside its rect.
struct Meter {
    float current = 0.0f, max = 0.0f;
    float fraction() const noexcept {
        if (!(max > 0.0f) || !std::isfinite(max) || !std::isfinite(current)) return 0.0f;
        return std::clamp(current / max, 0.0f, 1.0f);
    }
};

struct DamageNumber {
    float amount = 0.0f;
    float x = 0.0f, y = 0.0f, z = 0.0f; // world-space hit point the number floats above
    float age = 0.0f;                   // seconds since the hit
};
inline constexpr float kDamageNumberLifetime = 1.2f;

struct HudState {
    Meter health, stamina, vis, posture;
    std::vector<std::string> quickBelt; // item id per belt slot; empty string = empty slot
    std::size_t quickBeltSelected = 0;
    std::string activeResearch;         // research node being pursued; empty hides the panel
    float headingDeg = 0.0f;            // camera yaw driving the compass
    std::string eventNameKey;           // loc key of the nearest active event; empty hides the alert
    bool lockedOn = false;
    Prompt prompt = Prompt::None;
    bool damageNumbersEnabled = false;  // optional per Section 17, off by default
    std::vector<DamageNumber> damageNumbers;

    // Records a hit only when the option is on, so disabled numbers never accumulate.
    void addDamage(float amount, float x, float y, float z) {
        if (damageNumbersEnabled && std::isfinite(amount)) damageNumbers.push_back({amount, x, y, z, 0.0f});
    }
    // Ages damage numbers and drops expired ones; turning the option off clears them.
    void tick(float dt) {
        if (!damageNumbersEnabled) { damageNumbers.clear(); return; }
        for (DamageNumber& d : damageNumbers) d.age += dt;
        std::erase_if(damageNumbers, [](const DamageNumber& d) { return !(d.age < kDamageNumberLifetime); });
    }
};

// Compass bearing in [0, 360); a non-finite yaw reads as north.
inline float compassHeading(float yawDeg) noexcept {
    if (!std::isfinite(yawDeg)) return 0.0f;
    float h = std::fmod(yawDeg, 360.0f);
    if (h < 0.0f) h += 360.0f;
    return h >= 360.0f ? 0.0f : h;
}

// Health, stamina, Vis, posture and the compass are always on; the rest appear only when relevant.
inline bool isVisible(const HudState& s, HudElement e) noexcept {
    switch (e) {
    case HudElement::QuickBelt: return !s.quickBelt.empty();
    case HudElement::ActiveResearch: return !s.activeResearch.empty();
    case HudElement::EventAlert: return !s.eventNameKey.empty();
    case HudElement::LockOn: return s.lockedOn;
    case HudElement::InteractionPrompt: return s.prompt != Prompt::None && s.prompt < Prompt::Count;
    case HudElement::DamageNumbers: return s.damageNumbersEnabled;
    case HudElement::Count: return false;
    default: return true;
    }
}

// Screen-space dp layout. Bars sit top-left and posture top-centre, above the Look region of
// input::defaultTouchLayout (y >= 80) and clear of the movement stick and action cluster.
struct HudLayout {
    std::array<input::Rect, kHudElementCount> rects{};
    const input::Rect& operator[](HudElement e) const { return rects[static_cast<std::size_t>(e)]; }
    // Touch controls may never cover these (Section 22); feed them to touchLayoutIssues().
    std::vector<input::Rect> protectedRects() const {
        return {(*this)[HudElement::Health], (*this)[HudElement::Posture]};
    }
};

inline HudLayout buildHudLayout(float widthDp, float heightDp) noexcept {
    HudLayout l;
    auto set = [&l](HudElement e, input::Rect r) { l.rects[static_cast<std::size_t>(e)] = r; };
    const float cx = widthDp * 0.5f;
    set(HudElement::Health, {16.0f, 12.0f, 160.0f, 12.0f});
    set(HudElement::Stamina, {16.0f, 28.0f, 140.0f, 8.0f});
    set(HudElement::Vis, {16.0f, 40.0f, 140.0f, 8.0f});
    set(HudElement::ActiveResearch, {16.0f, 54.0f, 160.0f, 20.0f});
    set(HudElement::Posture, {cx - 100.0f, 12.0f, 200.0f, 10.0f});
    set(HudElement::Compass, {cx - 80.0f, 28.0f, 160.0f, 16.0f});
    set(HudElement::EventAlert, {widthDp - 196.0f, 12.0f, 180.0f, 36.0f});
    set(HudElement::LockOn, {cx - 16.0f, heightDp * 0.5f - 16.0f, 32.0f, 32.0f});
    set(HudElement::InteractionPrompt, {cx - 60.0f, heightDp * 0.62f, 120.0f, 28.0f});
    set(HudElement::QuickBelt, {16.0f, heightDp - 236.0f, 192.0f, 48.0f});
    set(HudElement::DamageNumbers, {0.0f, 0.0f, widthDp, heightDp});
    return l;
}

// ---- Settings menu (Sections 14, 17) ----

// Rows in Section 17 order. Value rows edit game::Settings in place; Touch Layout, Audio Mix and
// Save Recovery open their own pages.
enum class SettingsRow : std::uint8_t {
    GraphicsTier = 0, InternalResolution, CameraDistance, MotionReduction, Colourblind, Subtitles,
    FontScale, ControllerLayout, TouchLayout, Vibration, AudioMix, SaveRecovery, Count
};
inline constexpr std::size_t kSettingsRowCount = static_cast<std::size_t>(SettingsRow::Count);
inline constexpr std::array<std::string_view, kSettingsRowCount> kSettingsKeys{
    "settings.graphics_tier", "settings.internal_resolution", "settings.camera_distance",
    "settings.motion_reduction", "settings.colourblind", "settings.subtitles", "settings.font_scale",
    "settings.controller_layout", "settings.touch_layout", "settings.vibration", "settings.audio_mix",
    "settings.save_recovery"};
inline constexpr std::string_view kSettingsTitleKey = "menu.settings";

constexpr std::string_view locKey(SettingsRow r) noexcept {
    return r < SettingsRow::Count ? kSettingsKeys[static_cast<std::size_t>(r)] : std::string_view{};
}

enum class SettingsPage : std::uint8_t { None = 0, TouchLayout, AudioMix, SaveRecovery };

constexpr SettingsPage pageFor(SettingsRow r) noexcept {
    switch (r) {
    case SettingsRow::TouchLayout: return SettingsPage::TouchLayout;
    case SettingsRow::AudioMix: return SettingsPage::AudioMix;
    case SettingsRow::SaveRecovery: return SettingsPage::SaveRecovery;
    default: return SettingsPage::None;
    }
}

// What the settings screen needs from the platform layer and saves/ when it opens.
struct SettingsContext {
    render::RenderTier bestTier = render::RenderTier::DesktopMedium; // render::selectTier() result
    bool touchDevice = false;           // the touch layout page exists only where touch input does
    std::size_t recoverableBackups = 0; // valid .bak1/.bak2 envelopes found for the active saves
};

// Tiers the player may pick: a desktop may step down from Ultra to Medium but never above what the
// GPU reported, and Android stays on its GLES tier.
inline std::vector<render::RenderTier> selectableTiers(render::RenderTier best) {
    if (best == render::RenderTier::DesktopUltra)
        return {render::RenderTier::DesktopUltra, render::RenderTier::DesktopMedium};
    return {best};
}

// A row is greyed out when it has nothing to change: a single selectable tier, a fixed internal
// resolution (desktop tiers render at 1080p), no touch screen, or no backup to recover from.
inline bool rowEnabled(SettingsRow r, const Settings& s, const SettingsContext& ctx) {
    switch (r) {
    case SettingsRow::GraphicsTier: return selectableTiers(ctx.bestTier).size() > 1;
    case SettingsRow::InternalResolution: {
        const render::TierFeatures f = render::featuresFor(s.graphicsTier);
        return f.internalResMin < f.internalResMax;
    }
    case SettingsRow::TouchLayout: return ctx.touchDevice;
    case SettingsRow::SaveRecovery: return ctx.recoverableBackups > 0;
    case SettingsRow::Count: return false;
    default: return true;
    }
}

struct SettingsItem {
    SettingsRow row = SettingsRow::GraphicsTier;
    std::string_view key;
    bool enabled = true;
};

struct SettingsMenu {
    std::string_view titleKey = kSettingsTitleKey;
    std::vector<SettingsItem> items; // all twelve rows in Section 17 order; disabled rows stay listed
    std::size_t focus = 0;           // always an enabled row; Camera Distance is never disabled

    const SettingsItem& focused() const { return items[focus]; }

    // Up/down navigation: wraps and skips disabled rows, like MainMenu::move.
    void move(int delta) noexcept {
        if (items.empty() || delta == 0) return;
        const std::size_t n = items.size();
        const std::size_t step = delta > 0 ? 1 : n - 1;
        std::size_t i = focus;
        do { i = (i + step) % n; } while (!items[i].enabled && i != focus);
        focus = i;
    }

    // Confirm on a page row opens that page; value rows and disabled rows open nothing.
    SettingsPage activate() const noexcept {
        return items.empty() || !items[focus].enabled ? SettingsPage::None : pageFor(items[focus].row);
    }
};

inline SettingsMenu buildSettingsMenu(const Settings& s, const SettingsContext& ctx) {
    SettingsMenu m;
    m.items.reserve(kSettingsRowCount);
    for (std::size_t i = 0; i < kSettingsRowCount; ++i) {
        const auto r = static_cast<SettingsRow>(i);
        m.items.push_back(SettingsItem{r, kSettingsKeys[i], rowEnabled(r, s, ctx)});
    }
    while (m.focus < m.items.size() && !m.items[m.focus].enabled) ++m.focus;
    return m;
}

// ---- Settings values (Sections 14, 17) ----
// Left/right on a value row edits game::Settings in place: lists (graphics tier, colourblind
// palette, subtitles, controller layout) wrap, numeric rows step and stop at their ends, and
// toggles flip on either direction. Every edit ends in sanitize(), so no illegal value is stored.

inline constexpr int kInternalResolutionStep = 64; // px
inline constexpr float kCameraDistanceStep = 0.5f; // metres
inline constexpr float kFontScaleStep = 0.25f;

// Internal resolution choices for a tier: the minimum, then 64 px steps, ending on the maximum. A
// step within half a step of the maximum folds into it, so the C31 tier offers 512/576/640/720.
inline std::vector<int> resolutionSteps(render::RenderTier tier) {
    const render::TierFeatures f = render::featuresFor(tier);
    std::vector<int> out;
    for (int r = f.internalResMin; r < f.internalResMax; r += kInternalResolutionStep) out.push_back(r);
    if (out.size() > 1 && f.internalResMax - out.back() < kInternalResolutionStep / 2)
        out.back() = f.internalResMax;
    else
        out.push_back(f.internalResMax);
    return out;
}

namespace settings_detail {
// Next list index in the direction of dir, wrapping at both ends.
constexpr std::size_t cycle(std::size_t i, std::size_t n, int dir) noexcept {
    return n == 0 ? 0 : (i % n + (dir > 0 ? 1 : n - 1)) % n;
}
template <typename E>
constexpr void cycleEnum(E& e, std::size_t n, int dir) noexcept {
    e = static_cast<E>(cycle(static_cast<std::size_t>(e), n, dir));
}
// Moves v to the next multiple of step in the direction of dir (an off-grid value snaps to the
// neighbouring grid line), then clamps into [lo, hi].
inline void stepFloat(float& v, float step, float lo, float hi, int dir) noexcept {
    const float k = v / step;
    const float next = dir > 0 ? (std::floor(k + 1e-4f) + 1.0f) * step : (std::ceil(k - 1e-4f) - 1.0f) * step;
    v = std::clamp(next, lo, hi);
}
// Next resolution step strictly above (dir > 0) or below the current value; none past the ends.
inline void stepResolution(Settings& s, int dir) {
    const std::vector<int> steps = resolutionSteps(s.graphicsTier);
    if (dir > 0) {
        for (const int r : steps)
            if (r > s.internalResolution) { s.internalResolution = r; return; }
    } else {
        for (auto it = steps.rbegin(); it != steps.rend(); ++it)
            if (*it < s.internalResolution) { s.internalResolution = *it; return; }
    }
}
} // namespace settings_detail

// One left (dir < 0) or right (dir > 0) press on a value row. Page rows, disabled rows and dir == 0
// change nothing. Returns true when the stored settings changed.
inline bool adjustSetting(Settings& s, SettingsRow r, int dir, const SettingsContext& ctx) {
    if (dir == 0 || r >= SettingsRow::Count || pageFor(r) != SettingsPage::None || !rowEnabled(r, s, ctx))
        return false;
    Settings next = s;
    sanitize(next);
    switch (r) {
    case SettingsRow::GraphicsTier: {
        const std::vector<render::RenderTier> tiers = selectableTiers(ctx.bestTier);
        const auto it = std::find(tiers.begin(), tiers.end(), next.graphicsTier);
        const render::RenderTier tier =
            it == tiers.end()
                ? tiers.front()
                : tiers[settings_detail::cycle(static_cast<std::size_t>(it - tiers.begin()), tiers.size(), dir)];
        if (tier != next.graphicsTier) {
            next.graphicsTier = tier;
            next.internalResolution = render::featuresFor(tier).internalResDefault; // ranges differ per tier
        }
        break;
    }
    case SettingsRow::InternalResolution: settings_detail::stepResolution(next, dir); break;
    case SettingsRow::CameraDistance:
        settings_detail::stepFloat(next.cameraDistance, kCameraDistanceStep, kCameraDistanceMin,
                                   kCameraDistanceMax, dir);
        break;
    case SettingsRow::MotionReduction: next.motionReduction = !next.motionReduction; break;
    case SettingsRow::Colourblind: settings_detail::cycleEnum(next.colourblind, kColourblindNames.size(), dir); break;
    case SettingsRow::Subtitles: settings_detail::cycleEnum(next.subtitles, kSubtitleNames.size(), dir); break;
    case SettingsRow::FontScale:
        settings_detail::stepFloat(next.fontScale, kFontScaleStep, kFontScaleMin, kFontScaleMax, dir);
        break;
    case SettingsRow::ControllerLayout:
        settings_detail::cycleEnum(next.controllerLayout, kControllerLayoutNames.size(), dir);
        break;
    case SettingsRow::Vibration: next.vibration = !next.vibration; break;
    default: return false;
    }
    sanitize(next);
    const bool changed = !(next == s);
    s = next;
    return changed;
}

// Left/right on the focused row. Enable rules are refreshed afterwards (a tier change can fix or
// free the internal resolution) and focus is kept on an enabled row.
inline bool adjustFocused(SettingsMenu& m, Settings& s, const SettingsContext& ctx, int dir) {
    if (m.focus >= m.items.size() || !m.items[m.focus].enabled) return false;
    if (!adjustSetting(s, m.items[m.focus].row, dir, ctx)) return false;
    for (SettingsItem& item : m.items) item.enabled = rowEnabled(item.row, s, ctx);
    if (!m.items[m.focus].enabled) m.move(1);
    return true;
}

// ---- Audio Mix page (Sections 17, 24) ----
// Opened from the Audio Mix settings row: one slider per game::AudioMix bus. Left/right steps a
// gain to the next 10% mark and stops at mute and full; the renderer shows each as a whole percent.
// Music, effects, voice and ambience are heard through master, so master is listed first.

enum class AudioBus : std::uint8_t { Master = 0, Music, Effects, Voice, Ambience, Count };
inline constexpr std::size_t kAudioBusCount = static_cast<std::size_t>(AudioBus::Count);
inline constexpr std::array<std::string_view, kAudioBusCount> kAudioBusKeys{
    "audio.master", "audio.music", "audio.effects", "audio.voice", "audio.ambience"};
inline constexpr std::string_view kAudioMixTitleKey = "settings.audio_mix";
inline constexpr float kAudioGainStep = 0.1f;

constexpr std::string_view locKey(AudioBus b) noexcept {
    return b < AudioBus::Count ? kAudioBusKeys[static_cast<std::size_t>(b)] : std::string_view{};
}

// The stored gain behind a slider, or nullptr for AudioBus::Count.
inline float* gainSlot(AudioMix& mix, AudioBus b) noexcept {
    switch (b) {
    case AudioBus::Master: return &mix.master;
    case AudioBus::Music: return &mix.music;
    case AudioBus::Effects: return &mix.effects;
    case AudioBus::Voice: return &mix.voice;
    case AudioBus::Ambience: return &mix.ambience;
    default: return nullptr;
    }
}
inline float gainOf(const AudioMix& mix, AudioBus b) noexcept {
    AudioMix copy = mix;
    const float* g = gainSlot(copy, b);
    return g != nullptr ? *g : 0.0f;
}

// Whole percentage beside a slider; an out-of-range or non-finite gain shows what sanitize() stores.
inline int gainPercent(float g) noexcept {
    return static_cast<int>(std::lround(profile_detail::clampOr(g, 0.0f, 1.0f, 1.0f) * 100.0f));
}

// Gain the mixer applies to a bus: its own slider times master (master alone for the master bus).
inline float effectiveGain(const AudioMix& mix, AudioBus b) noexcept {
    if (b >= AudioBus::Count) return 0.0f;
    const float master = profile_detail::clampOr(mix.master, 0.0f, 1.0f, 1.0f);
    if (b == AudioBus::Master) return master;
    return master * profile_detail::clampOr(gainOf(mix, b), 0.0f, 1.0f, 1.0f);
}

// One left (dir < 0) or right (dir > 0) press on a slider. An off-grid gain snaps to the
// neighbouring 10% mark. Returns true when the stored mix changed.
inline bool adjustGain(AudioMix& mix, AudioBus b, int dir) noexcept {
    float* g = gainSlot(mix, b);
    if (g == nullptr || dir == 0) return false;
    const float before = *g;
    float next = profile_detail::clampOr(before, 0.0f, 1.0f, 1.0f);
    settings_detail::stepFloat(next, kAudioGainStep, 0.0f, 1.0f, dir);
    *g = next;
    return !(next == before);
}

struct AudioSlider {
    AudioBus bus = AudioBus::Master;
    std::string_view key;
    int percent = 100;
};

struct AudioMixPage {
    std::string_view titleKey = kAudioMixTitleKey;
    AudioBus focus = AudioBus::Master; // every slider is always enabled

    // Up/down navigation, wrapping at both ends.
    void move(int delta) noexcept {
        if (delta != 0) settings_detail::cycleEnum(focus, kAudioBusCount, delta);
    }

    // Rows in bus order with their current percentages, for the renderer.
    std::array<AudioSlider, kAudioBusCount> sliders(const AudioMix& mix) const noexcept {
        std::array<AudioSlider, kAudioBusCount> out{};
        for (std::size_t i = 0; i < kAudioBusCount; ++i) {
            const auto b = static_cast<AudioBus>(i);
            out[i] = AudioSlider{b, kAudioBusKeys[i], gainPercent(gainOf(mix, b))};
        }
        return out;
    }
};

// Left/right on the focused slider edits Settings::audio in place.
inline bool adjustFocused(const AudioMixPage& p, Settings& s, int dir) noexcept {
    return adjustGain(s.audio, p.focus, dir);
}

// ---- Touch Layout page (Sections 17, 22) ----
// Opened from the Touch Layout settings row on touch devices. Row 0 sets the global button size
// (Settings::touchScale), one row per Section 17 control follows in kTouchActions order, and Reset
// comes last. Left/right on a size row steps 10%; confirm on a control row shows or hides it (Move
// and Look are regions and stay shown); confirm on Reset restores the TCore defaults. An edit is
// kept only when it adds no Section 22 issue on this screen (every visible control a 9dp target,
// none over health/posture), so a layout that passed the gate can never be saved broken.

inline constexpr std::size_t kTouchControlCount = kTouchActions.size();
inline constexpr std::size_t kTouchRowCount = kTouchControlCount + 2; // global size, controls, reset
inline constexpr std::size_t kTouchResetRow = kTouchRowCount - 1;
inline constexpr std::array<std::string_view, kTouchControlCount> kTouchControlKeys{
    "touch.move", "touch.look",  "touch.attack",   "touch.guard",     "touch.dodge",
    "touch.jump", "touch.spell", "touch.interact", "touch.item_belt", "touch.lock_on"};
inline constexpr std::string_view kTouchTitleKey = "settings.touch_layout";
inline constexpr std::string_view kTouchSizeKey = "touch.size";
inline constexpr std::string_view kTouchResetKey = "touch.reset";
inline constexpr float kTouchScaleStep = 0.1f;

// Outcome of one edit, for the page's status line: a rejected edit names the rule it would break.
enum class TouchEdit : std::uint8_t { None = 0, Changed, TooSmall, CoversHud };

constexpr std::string_view locKey(TouchEdit e) noexcept {
    switch (e) {
    case TouchEdit::TooSmall: return "touch.too_small";
    case TouchEdit::CoversHud: return "touch.covers_hud";
    default: return {};
    }
}

enum class TouchRowKind : std::uint8_t { GlobalSize = 0, Control, Reset };

struct TouchRow {
    TouchRowKind kind = TouchRowKind::GlobalSize;
    input::Action action = input::Action::Move; // Control rows only
    std::string_view key;
    int percent = 100;     // size rows: the stored size as a whole percent
    bool visible = true;   // Control rows
    bool hideable = false; // Control rows other than Move and Look
};

// Whole percentage beside a size row; an out-of-range or non-finite size shows what sanitize() stores.
inline int touchPercent(float scale) noexcept {
    return static_cast<int>(std::lround(profile_detail::clampOr(scale, kTouchScaleMin, kTouchScaleMax, 1.0f) * 100.0f));
}

namespace touch_detail {
// Commits a candidate layout unless it adds an issue the stored layout does not already have, so
// a layout that already fails on a tiny screen can still be edited towards passing.
inline TouchEdit commit(Settings& s, TouchLayoutPrefs& t, float scale, const TouchLayoutPrefs& next,
                        float widthDp, float heightDp) {
    if (scale == s.touchScale && next == t) return TouchEdit::None;
    const std::vector<input::Rect> hud = buildHudLayout(widthDp, heightDp).protectedRects();
    const auto before = touchLayoutIssues(t, s.touchScale, widthDp, heightDp, hud);
    for (const input::TouchIssue& a : touchLayoutIssues(next, scale, widthDp, heightDp, hud)) {
        const bool known = std::any_of(before.begin(), before.end(), [&a](const input::TouchIssue& b) {
            return b.action == a.action && b.kind == a.kind;
        });
        if (!known) return a.kind == input::TouchIssue::TooSmall ? TouchEdit::TooSmall : TouchEdit::CoversHud;
    }
    s.touchScale = scale;
    t = next;
    return TouchEdit::Changed;
}
} // namespace touch_detail

struct TouchLayoutPage {
    std::string_view titleKey = kTouchTitleKey;
    std::size_t focus = 0;              // every row is always enabled
    TouchEdit last = TouchEdit::None;   // result of the latest edit, shown until the next one

    // Up/down navigation, wrapping at both ends.
    void move(int delta) noexcept {
        if (delta != 0) focus = settings_detail::cycle(focus, kTouchRowCount, delta);
    }

    // The control a row edits, or Action::Count on the global size and Reset rows.
    static constexpr input::Action actionAt(std::size_t row) noexcept {
        return row >= 1 && row <= kTouchControlCount ? kTouchActions[row - 1] : input::Action::Count;
    }

    // Rows in page order with their current values, for the renderer.
    std::array<TouchRow, kTouchRowCount> rows(const Settings& s, const TouchLayoutPrefs& t) const noexcept {
        std::array<TouchRow, kTouchRowCount> out{};
        out[0] = TouchRow{TouchRowKind::GlobalSize, input::Action::Move, kTouchSizeKey, touchPercent(s.touchScale)};
        for (std::size_t i = 0; i < kTouchControlCount; ++i) {
            const input::Action a = kTouchActions[i];
            const bool hideable = touchHideable(a);
            out[i + 1] = TouchRow{TouchRowKind::Control, a, kTouchControlKeys[i], touchPercent(t.buttons[i].scale),
                                  t.buttons[i].visible || !hideable, hideable};
        }
        out[kTouchResetRow] = TouchRow{TouchRowKind::Reset, input::Action::Move, kTouchResetKey, 100};
        return out;
    }
};

// Left/right on the global size row or a control row steps that size 10% within
// [kTouchScaleMin, kTouchScaleMax]; an off-grid size snaps to the neighbouring 10% mark and a corrupt
// one steps from what sanitize() stores. The Reset row and dir == 0 change nothing. The result is
// also kept in p.last for the status line.
inline TouchEdit adjustFocused(TouchLayoutPage& p, Settings& s, TouchLayoutPrefs& t, int dir, float widthDp,
                               float heightDp) {
    TouchEdit r = TouchEdit::None;
    if (dir != 0 && p.focus < kTouchResetRow) {
        float scale = profile_detail::clampOr(s.touchScale, kTouchScaleMin, kTouchScaleMax, 1.0f);
        TouchLayoutPrefs next = t;
        sanitize(next);
        float& slot = p.focus == 0 ? scale : next.buttons[p.focus - 1].scale;
        settings_detail::stepFloat(slot, kTouchScaleStep, kTouchScaleMin, kTouchScaleMax, dir);
        r = touch_detail::commit(s, t, scale, next, widthDp, heightDp);
    }
    p.last = r;
    return r;
}

// Confirm on a hideable control row shows or hides it. Hiding never adds an issue; showing is kept
// only when the gate allows. Confirm on Reset restores the TCore default layout at 100% size and
// always applies, so the player can recover from any layout. The global size row, Move and Look
// ignore confirm.
inline TouchEdit confirmFocused(TouchLayoutPage& p, Settings& s, TouchLayoutPrefs& t, float widthDp,
                                float heightDp) {
    TouchEdit r = TouchEdit::None;
    if (p.focus == kTouchResetRow) {
        const TouchLayoutPrefs defaults{};
        if (!(s.touchScale == 1.0f) || !(t == defaults)) {
            s.touchScale = 1.0f;
            t = defaults;
            r = TouchEdit::Changed;
        }
    } else if (const input::Action a = TouchLayoutPage::actionAt(p.focus);
               a != input::Action::Count && touchHideable(a)) {
        TouchLayoutPrefs next = t;
        sanitize(next);
        TouchButtonPref& b = next.buttons[p.focus - 1];
        b.visible = !b.visible;
        const float scale = profile_detail::clampOr(s.touchScale, kTouchScaleMin, kTouchScaleMax, 1.0f);
        r = touch_detail::commit(s, t, scale, next, widthDp, heightDp);
    }
    p.last = r;
    return r;
}

// ---- Save Recovery page (Sections 14, 17, 19) ----
// Opened from the Save Recovery settings row. One row per save file the platform layer found under
// saves/ (profile.json, the character save, world_<seed>.tmw, journal_<region>.tmj, the co-op
// ledger), showing what tools::inspectSaveChain() predicts SaveFile::loadWithRecovery will do: load
// the main file, fall back to .bak1/.bak2, or find no usable copy, with a warning when a leftover
// .tmp shows a write was interrupted. Confirm on a damaged row runs tools::recoverSave(): the newest
// good backup is restored over the main file and the damaged copy is kept as <save>.corrupt for bug
// reports. Rows are re-inspected after every confirm, so the page always shows what is on disk.

struct SaveSlot {
    core::SaveLayer layer = core::SaveLayer::Profile;
    std::filesystem::path path;            // main file; .bak1/.bak2/.tmp sit beside it
    std::uint32_t maxSupportedVersion = 1; // newest schema this build reads for the layer
};

enum class SaveState : std::uint8_t { Good = 0, Recoverable, Lost };
enum class SaveRestore : std::uint8_t { None = 0, Restored, Failed };

inline constexpr std::string_view kSaveRecoveryTitleKey = "settings.save_recovery";
inline constexpr std::string_view kSaveInterruptedKey = "save.interrupted";
inline constexpr std::array<std::string_view, 5> kSaveLayerKeys{
    "save.profile", "save.character", "save.world_seed", "save.world_journal", "save.coop_ledger"};

// Layer names follow core::SaveLayer ids 1..5.
constexpr std::string_view locKey(core::SaveLayer l) noexcept {
    const auto i = static_cast<std::size_t>(l);
    return i >= 1 && i <= kSaveLayerKeys.size() ? kSaveLayerKeys[i - 1] : std::string_view{};
}

// "save.recoverable" takes the backup number ({0} = 1 or 2) that confirm would restore.
constexpr std::string_view locKey(SaveState s) noexcept {
    switch (s) {
    case SaveState::Good: return "save.good";
    case SaveState::Recoverable: return "save.recoverable";
    case SaveState::Lost: return "save.lost";
    }
    return {};
}

// Status line after a confirm; "save.restored" takes the backup number that was restored.
constexpr std::string_view locKey(SaveRestore r) noexcept {
    switch (r) {
    case SaveRestore::Restored: return "save.restored";
    case SaveRestore::Failed: return "save.restore_failed";
    default: return {};
    }
}

struct SaveRecoveryRow {
    core::SaveLayer layer = core::SaveLayer::Profile;
    std::string_view key;          // layer name
    SaveState state = SaveState::Lost;
    std::string_view stateKey;
    int backup = 0;                // Recoverable rows: the backup confirm restores (1 or 2)
    bool interruptedWrite = false; // shown with kSaveInterruptedKey
};

// The row for one save as it is on disk right now.
inline SaveRecoveryRow inspectSlot(const SaveSlot& slot) {
    const tools::SaveChainReport c = tools::inspectSaveChain(slot.path, slot.layer, slot.maxSupportedVersion);
    SaveRecoveryRow r;
    r.layer = slot.layer;
    r.key = locKey(slot.layer);
    r.state = c.healthy() ? SaveState::Good : c.needsRecovery() ? SaveState::Recoverable : SaveState::Lost;
    r.stateKey = locKey(r.state);
    r.backup = c.needsRecovery() ? c.recoverySource : 0;
    r.interruptedWrite = c.interruptedWrite;
    return r;
}

// Valid .bak1/.bak2 envelopes across the active saves. The platform layer stores this in
// SettingsContext::recoverableBackups, which enables the Save Recovery row.
inline std::size_t countRecoverableBackups(const std::vector<SaveSlot>& slots) {
    std::size_t n = 0;
    for (const SaveSlot& s : slots) {
        const tools::SaveChainReport c = tools::inspectSaveChain(s.path, s.layer, s.maxSupportedVersion);
        n += (c.files[1].usable() ? 1u : 0u) + (c.files[2].usable() ? 1u : 0u);
    }
    return n;
}

struct SaveRecoveryPage {
    std::string_view titleKey = kSaveRecoveryTitleKey;
    std::vector<SaveSlot> slots;          // active saves, in the order the platform layer listed them
    std::vector<SaveRecoveryRow> rows;    // one per slot, as on disk at the latest refresh()
    std::size_t focus = 0;                // every row is focusable; only Recoverable rows act on confirm
    SaveRestore last = SaveRestore::None; // result of the latest confirm, shown until the next one
    int lastBackup = 0;                   // backup number for "save.restored"

    // Re-inspects every slot; focus stays on the same index, clamped to the row count.
    void refresh() {
        rows.clear();
        rows.reserve(slots.size());
        for (const SaveSlot& s : slots) rows.push_back(inspectSlot(s));
        if (focus >= rows.size()) focus = rows.empty() ? 0 : rows.size() - 1;
    }

    // Up/down navigation, wrapping at both ends.
    void move(int delta) noexcept {
        if (delta != 0) focus = settings_detail::cycle(focus, rows.size(), delta);
    }

    // True while any row still has a backup confirm could restore.
    bool anyRecoverable() const noexcept {
        return std::any_of(rows.begin(), rows.end(),
                           [](const SaveRecoveryRow& r) { return r.state == SaveState::Recoverable; });
    }
};

inline SaveRecoveryPage buildSaveRecoveryPage(const std::vector<SaveSlot>& slots) {
    SaveRecoveryPage p;
    p.slots = slots;
    p.refresh();
    return p;
}

// Confirm on a Recoverable row runs tools::recoverSave() for that slot and reports Restored with the
// backup number, or Failed when the restore could not complete or no usable copy is left. Good and
// Lost rows ignore confirm, as does a row whose main file turned good since the last refresh. The
// page is re-inspected afterwards either way.
inline SaveRestore confirmFocused(SaveRecoveryPage& p) {
    SaveRestore r = SaveRestore::None;
    int backup = 0;
    if (p.focus < p.rows.size() && p.focus < p.slots.size() && p.rows[p.focus].state == SaveState::Recoverable) {
        const SaveSlot& slot = p.slots[p.focus];
        const tools::SaveRecoveryResult res = tools::recoverSave(slot.path, slot.layer, slot.maxSupportedVersion);
        if (res.restored) {
            r = SaveRestore::Restored;
            backup = res.source;
        } else if (res.source != 0) {
            r = SaveRestore::Failed;
        }
    }
    p.refresh();
    p.last = r;
    p.lastBackup = backup;
    return r;
}

} // namespace lt::game::ui

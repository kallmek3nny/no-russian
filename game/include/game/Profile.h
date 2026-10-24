#pragma once
// Profile save layer (Sections 14, 17, 22): settings, accessibility and unlocked research live in
// saves/profile.json, apart from character and world saves. Loading sanitizes every value into its
// legal range, so a hand-edited or older profile can never leave the settings menu in a bad state.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <vector>

#include "tcore/Input.h"
#include "trender/RenderTier.h"

namespace lt::game {

enum class ColourblindPalette : std::uint8_t { Off = 0, Protanopia, Deuteranopia, Tritanopia };
enum class SubtitleMode : std::uint8_t { Off = 0, Dialogue, DialogueAndCues };
enum class ControllerLayout : std::uint8_t { Standard = 0, Southpaw, Legacy };

inline constexpr std::array<std::string_view, 4> kColourblindNames{"off", "protanopia", "deuteranopia", "tritanopia"};
inline constexpr std::array<std::string_view, 3> kSubtitleNames{"off", "dialogue", "dialogue_and_cues"};
inline constexpr std::array<std::string_view, 3> kControllerLayoutNames{"standard", "southpaw", "legacy"};
namespace profile_detail {
template <typename E, std::size_t N>
constexpr std::string_view nameOf(E e, const std::array<std::string_view, N>& names) noexcept {
    return static_cast<std::size_t>(e) < N ? names[static_cast<std::size_t>(e)] : std::string_view{};
}
template <typename E, std::size_t N>
constexpr std::optional<E> fromName(std::string_view s, const std::array<std::string_view, N>& names) noexcept {
    for (std::size_t i = 0; i < N; ++i)
        if (names[i] == s) return static_cast<E>(i);
    return std::nullopt;
}
// NaN-safe clamp: a non-finite value falls back to the default instead of propagating.
inline float clampOr(float v, float lo, float hi, float def) noexcept {
    return std::isfinite(v) ? std::clamp(v, lo, hi) : def;
}
} // namespace profile_detail

inline std::optional<render::RenderTier> tierFromName(std::string_view s) noexcept {
    for (const auto t : {render::RenderTier::DesktopUltra, render::RenderTier::DesktopMedium,
                         render::RenderTier::AndroidC31})
        if (render::toString(t) == s) return t;
    return std::nullopt;
}

inline constexpr float kCameraDistanceMin = 2.0f, kCameraDistanceMax = 8.0f; // metres behind the player
inline constexpr float kFontScaleMin = 0.75f, kFontScaleMax = 2.0f;
inline constexpr float kTouchScaleMin = 0.5f, kTouchScaleMax = 2.0f; // the 9dp rule still applies after scaling

struct AudioMix { // mix buses, each a linear gain in [0, 1]
    float master = 1.0f, music = 0.8f, effects = 1.0f, voice = 1.0f, ambience = 0.8f;
    friend bool operator==(const AudioMix&, const AudioMix&) = default;
};

// Section 17 settings menu (controller/touch layouts and save recovery are layered on below).
struct Settings {
    render::RenderTier graphicsTier = render::RenderTier::DesktopMedium;
    int internalResolution = 1080; // render height in px, kept inside the tier's range
    float cameraDistance = 4.0f;
    bool motionReduction = false;
    ColourblindPalette colourblind = ColourblindPalette::Off;
    SubtitleMode subtitles = SubtitleMode::Dialogue;
    float fontScale = 1.0f;
    ControllerLayout controllerLayout = ControllerLayout::Standard;
    float touchScale = 1.0f; // global touch-button size multiplier
    bool vibration = true;
    AudioMix audio;
    friend bool operator==(const Settings&, const Settings&) = default;
};

// Platform defaults: the tier's default internal resolution, and larger UI text on Android.
inline Settings defaultSettings(render::RenderTier tier) noexcept {
    Settings s;
    s.graphicsTier = tier;
    s.internalResolution = render::featuresFor(tier).internalResDefault;
    if (tier == render::RenderTier::AndroidC31) s.fontScale = 1.25f;
    return s;
}

// Clamps every field into its legal range; returns true when anything had to change.
inline bool sanitize(Settings& s) noexcept {
    using profile_detail::clampOr;
    const Settings before = s;
    if (s.graphicsTier > render::RenderTier::AndroidC31) s.graphicsTier = render::RenderTier::DesktopMedium;
    if (s.colourblind > ColourblindPalette::Tritanopia) s.colourblind = ColourblindPalette::Off;
    if (s.subtitles > SubtitleMode::DialogueAndCues) s.subtitles = SubtitleMode::Dialogue;
    if (s.controllerLayout > ControllerLayout::Legacy) s.controllerLayout = ControllerLayout::Standard;
    const render::TierFeatures f = render::featuresFor(s.graphicsTier);
    s.internalResolution = std::clamp(s.internalResolution, f.internalResMin, f.internalResMax);
    s.cameraDistance = clampOr(s.cameraDistance, kCameraDistanceMin, kCameraDistanceMax, 4.0f);
    s.fontScale = clampOr(s.fontScale, kFontScaleMin, kFontScaleMax, 1.0f);
    s.touchScale = clampOr(s.touchScale, kTouchScaleMin, kTouchScaleMax, 1.0f);
    for (float* g : {&s.audio.master, &s.audio.music, &s.audio.effects, &s.audio.voice, &s.audio.ambience})
        *g = clampOr(*g, 0.0f, 1.0f, 1.0f);
    return !(s == before);
}

// ---- Touch layout preferences (Sections 17, 22) ----

// The Section 17 Android control set, in defaultTouchLayout() order. Move and Look are regions
// rather than buttons, so they can be resized but never hidden.
inline constexpr std::array<input::Action, 10> kTouchActions{
    input::Action::Move,      input::Action::Look,  input::Action::AttackLight, input::Action::Guard,
    input::Action::Dodge,     input::Action::Jump,  input::Action::Cast,        input::Action::Interact,
    input::Action::QuickItem, input::Action::LockOn};

constexpr bool touchHideable(input::Action a) noexcept {
    return a != input::Action::Move && a != input::Action::Look;
}

struct TouchButtonPref {
    float scale = 1.0f; // per-control size, applied on top of Settings::touchScale
    bool visible = true;
    friend bool operator==(const TouchButtonPref&, const TouchButtonPref&) = default;
};

struct TouchLayoutPrefs {
    std::array<TouchButtonPref, kTouchActions.size()> buttons{};

    static constexpr std::optional<std::size_t> indexOf(input::Action a) noexcept {
        for (std::size_t i = 0; i < kTouchActions.size(); ++i)
            if (kTouchActions[i] == a) return i;
        return std::nullopt;
    }
    TouchButtonPref* find(input::Action a) noexcept {
        const auto i = indexOf(a);
        return i ? &buttons[*i] : nullptr;
    }
    const TouchButtonPref* find(input::Action a) const noexcept {
        const auto i = indexOf(a);
        return i ? &buttons[*i] : nullptr;
    }
    friend bool operator==(const TouchLayoutPrefs&, const TouchLayoutPrefs&) = default;
};

inline bool sanitize(TouchLayoutPrefs& t) noexcept {
    const TouchLayoutPrefs before = t;
    for (std::size_t i = 0; i < kTouchActions.size(); ++i) {
        TouchButtonPref& b = t.buttons[i];
        b.scale = profile_detail::clampOr(b.scale, kTouchScaleMin, kTouchScaleMax, 1.0f);
        if (!touchHideable(kTouchActions[i])) b.visible = true;
    }
    return !(t == before);
}

// On-screen layout for one screen size: TCore default positions, each control scaled about its
// centre by its own size times the global multiplier, and hidden buttons flagged invisible.
inline std::vector<input::TouchButton> buildTouchLayout(const TouchLayoutPrefs& prefs, float globalScale,
                                                        float widthDp, float heightDp) {
    std::vector<input::TouchButton> out = input::defaultTouchLayout(widthDp, heightDp);
    for (input::TouchButton& b : out) {
        const TouchButtonPref* p = prefs.find(b.action);
        if (p == nullptr) continue;
        const float k = p->scale * globalScale;
        const float cx = b.rectDp.x + b.rectDp.w * 0.5f;
        const float cy = b.rectDp.y + b.rectDp.h * 0.5f;
        b.rectDp.w *= k;
        b.rectDp.h *= k;
        b.rectDp.x = cx - b.rectDp.w * 0.5f;
        b.rectDp.y = cy - b.rectDp.h * 0.5f;
        b.visible = p->visible || !touchHideable(b.action);
    }
    return out;
}

// Section 22 gate for the settings menu: a layout is accepted only when every visible control keeps
// a 9dp target after scaling and stays clear of the health/posture HUD rects.
inline std::vector<input::TouchIssue> touchLayoutIssues(const TouchLayoutPrefs& prefs, float globalScale,
                                                        float widthDp, float heightDp,
                                                        const std::vector<input::Rect>& hud) {
    return input::validateTouchLayout(buildTouchLayout(prefs, globalScale, widthDp, heightDp), 1.0f, hud);
}

} // namespace lt::game

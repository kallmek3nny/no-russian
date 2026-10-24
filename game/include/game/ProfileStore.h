#pragma once
// Profile persistence (Sections 14, 17): the profile save layer as JSON text. Settings, touch layout
// prefs and unlocked research map to stable snake_case keys; missing or mistyped keys keep platform
// defaults and loaded values are sanitized, so old or hand-edited profiles always load legally.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "game/Profile.h"
#include "tcore/Json.h"
#include "tcore/JsonParser.h"
#include "tcore/SaveFile.h"

namespace lt::game {

struct Profile {
    Settings settings;
    TouchLayoutPrefs touch;
    std::vector<std::string> unlockedResearch; // research ids, sorted and unique
    friend bool operator==(const Profile&, const Profile&) = default;
};

inline Profile defaultProfile(render::RenderTier tier) { return Profile{defaultSettings(tier), {}, {}}; }

namespace profile_detail {
inline constexpr auto kIdLess = [](std::string_view a, std::string_view b) noexcept { return a < b; };

// Floats are stored in their shortest round-tripping decimal form (0.8, not 0.800000011920929).
inline double tidyFloat(float v) noexcept {
    if (!std::isfinite(v)) return static_cast<double>(v); // dumped as null, read back as the default
    char buf[32] = {};
    for (int precision = 1; precision <= 9; ++precision) { // 9 significant digits always round-trip
        std::snprintf(buf, sizeof buf, "%.*g", precision, static_cast<double>(v));
        if (std::strtof(buf, nullptr) == v) break;
    }
    return std::strtod(buf, nullptr);
}
} // namespace profile_detail

inline bool hasResearch(const Profile& p, std::string_view id) noexcept {
    return std::binary_search(p.unlockedResearch.begin(), p.unlockedResearch.end(), id,
                              profile_detail::kIdLess);
}

// Records a research unlock; returns false for an empty id or one that is already unlocked.
inline bool unlockResearch(Profile& p, std::string_view id) {
    if (id.empty()) return false;
    std::vector<std::string>& v = p.unlockedResearch;
    const auto it = std::lower_bound(v.begin(), v.end(), id, profile_detail::kIdLess);
    if (it != v.end() && std::string_view(*it) == id) return false;
    v.insert(it, std::string(id));
    return true;
}

// Sanitizes settings and touch prefs and normalizes research ids; true when anything changed.
inline bool sanitize(Profile& p) {
    const bool settingsChanged = sanitize(p.settings);
    const bool touchChanged = sanitize(p.touch);
    std::vector<std::string> ids = p.unlockedResearch;
    ids.erase(std::remove(ids.begin(), ids.end(), std::string{}), ids.end());
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    const bool researchChanged = ids != p.unlockedResearch;
    if (researchChanged) p.unlockedResearch = std::move(ids);
    return settingsChanged || touchChanged || researchChanged;
}

inline core::Json toJson(const AudioMix& a) {
    core::Json j = core::Json::object();
    using profile_detail::tidyFloat;
    j.set("master", tidyFloat(a.master)).set("music", tidyFloat(a.music));
    j.set("effects", tidyFloat(a.effects)).set("voice", tidyFloat(a.voice));
    j.set("ambience", tidyFloat(a.ambience));
    return j;
}

inline core::Json toJson(const Settings& s) {
    using profile_detail::nameOf;
    using profile_detail::tidyFloat;
    core::Json j = core::Json::object();
    j.set("graphics_tier", render::toString(s.graphicsTier));
    j.set("internal_resolution", s.internalResolution);
    j.set("camera_distance", tidyFloat(s.cameraDistance)).set("motion_reduction", s.motionReduction);
    j.set("colourblind", nameOf(s.colourblind, kColourblindNames));
    j.set("subtitles", nameOf(s.subtitles, kSubtitleNames)).set("font_scale", tidyFloat(s.fontScale));
    j.set("controller_layout", nameOf(s.controllerLayout, kControllerLayoutNames));
    j.set("touch_scale", tidyFloat(s.touchScale)).set("vibration", s.vibration);
    j.set("audio", toJson(s.audio));
    return j;
}

// Touch prefs are keyed by action name, so reordering kTouchActions never remaps a saved layout.
inline core::Json toJson(const TouchLayoutPrefs& t) {
    core::Json j = core::Json::object();
    for (std::size_t i = 0; i < kTouchActions.size(); ++i) {
        core::Json b = core::Json::object();
        b.set("scale", profile_detail::tidyFloat(t.buttons[i].scale)).set("visible", t.buttons[i].visible);
        j.set(std::string(input::actionName(kTouchActions[i])), std::move(b));
    }
    return j;
}

inline core::Json toJson(const Profile& p) {
    core::Json research = core::Json::array();
    for (const std::string& id : p.unlockedResearch) research.push(id);
    core::Json j = core::Json::object();
    j.set("settings", toJson(p.settings)).set("touch_layout", toJson(p.touch));
    j.set("unlocked_research", std::move(research));
    return j;
}

namespace profile_detail {
// Readers overlay one member onto an existing value; a missing or mistyped key keeps it unchanged.
inline void readFloat(const core::Json& j, std::string_view key, float& out) noexcept {
    const core::Json& v = j[key];
    // Bounded before narrowing: an out-of-range double -> float conversion is undefined behaviour.
    if (v.isNumber()) out = static_cast<float>(std::clamp(v.asNumber(), -1.0e30, 1.0e30));
}
inline void readBool(const core::Json& j, std::string_view key, bool& out) noexcept {
    const core::Json& v = j[key];
    if (v.isBool()) out = v.asBool();
}
template <typename E, std::size_t N>
void readEnum(const core::Json& j, std::string_view key, const std::array<std::string_view, N>& names,
              E& out) noexcept {
    if (const auto e = fromName<E>(j[key].str(), names)) out = *e;
}
} // namespace profile_detail

// Overlays the recognised members of a "settings" object onto s. Callers sanitize afterwards.
inline void readSettings(const core::Json& j, Settings& s) {
    using profile_detail::readBool;
    using profile_detail::readEnum;
    using profile_detail::readFloat;
    if (const auto t = tierFromName(j["graphics_tier"].str())) s.graphicsTier = *t;
    if (const core::Json& r = j["internal_resolution"]; r.isInteger())
        s.internalResolution = static_cast<int>(std::clamp<std::int64_t>(r.asInt(), -1000000, 1000000));
    readFloat(j, "camera_distance", s.cameraDistance);
    readBool(j, "motion_reduction", s.motionReduction);
    readEnum(j, "colourblind", kColourblindNames, s.colourblind);
    readEnum(j, "subtitles", kSubtitleNames, s.subtitles);
    readFloat(j, "font_scale", s.fontScale);
    readEnum(j, "controller_layout", kControllerLayoutNames, s.controllerLayout);
    readFloat(j, "touch_scale", s.touchScale);
    readBool(j, "vibration", s.vibration);
    const core::Json& a = j["audio"];
    readFloat(a, "master", s.audio.master);
    readFloat(a, "music", s.audio.music);
    readFloat(a, "effects", s.audio.effects);
    readFloat(a, "voice", s.audio.voice);
    readFloat(a, "ambience", s.audio.ambience);
}

// Overlays per-control prefs from a "touch_layout" object; unknown action keys are ignored.
inline void readTouchLayout(const core::Json& j, TouchLayoutPrefs& t) {
    for (std::size_t i = 0; i < kTouchActions.size(); ++i) {
        const core::Json& b = j[input::actionName(kTouchActions[i])];
        profile_detail::readFloat(b, "scale", t.buttons[i].scale);
        profile_detail::readBool(b, "visible", t.buttons[i].visible);
    }
}

// Platform defaults first, then every recognised member, then sanitize. Unknown keys are ignored
// so a profile written by a newer build still loads. *repaired is set when sanitize changed values.
inline Profile profileFromJson(const core::Json& j, render::RenderTier platformTier, bool* repaired = nullptr) {
    Profile p = defaultProfile(platformTier);
    readSettings(j["settings"], p.settings);
    readTouchLayout(j["touch_layout"], p.touch);
    p.unlockedResearch = j.stringList("unlocked_research");
    const bool changed = sanitize(p);
    if (repaired != nullptr) *repaired = changed;
    return p;
}

// ---- Persistence: saves/profile.json -------------------------------------------------------------
// The profile is stored as pretty-printed JSON text inside a TCore save envelope (layer Profile), so
// it gets the CRC32 check, the atomic temp-file write and the two rolling backups from SaveFile.

inline constexpr std::uint32_t kProfileSchemaVersion = 1;

// Section 14 data layout: the profile lives at saves/profile.json.
inline std::filesystem::path profilePath(const std::filesystem::path& savesDir) {
    return savesDir / "profile.json";
}

// Deterministic text form: 2-space indent, insertion-ordered keys, trailing newline.
inline std::string profileToText(const Profile& p) {
    std::string text = toJson(p).dump(2);
    text += '\n';
    return text;
}

// Parses profile JSON text; nullopt when it is not valid JSON or the root is not an object.
inline std::optional<Profile> profileFromText(std::string_view text, render::RenderTier platformTier,
                                              bool* repaired = nullptr) {
    const core::JsonParseResult r = core::parseJson(text);
    if (!r.ok() || !r.value.isObject()) return std::nullopt;
    return profileFromJson(r.value, platformTier, repaired);
}

// Sanitizes a copy, then writes it atomically; the previous files rotate to .bak1/.bak2.
inline bool saveProfile(const std::filesystem::path& path, const Profile& p) {
    Profile clean = p;
    static_cast<void>(sanitize(clean));
    const std::string text = profileToText(clean);
    core::SaveEnvelope e;
    e.layer = core::SaveLayer::Profile;
    e.schemaVersion = kProfileSchemaVersion;
    e.payload.assign(text.begin(), text.end());
    return core::SaveFile::writeAtomic(path, e);
}

struct ProfileLoadResult {
    Profile profile;       // platform defaults when nothing usable was found
    int source = -1;       // 0 main file, 1/2 rolling backups, -1 defaults
    bool repaired = false; // sanitize changed values read from disk
    bool loaded() const noexcept { return source >= 0; }
};

// Tries the main file, then .bak1, then .bak2. A candidate is skipped when it is missing, fails the
// envelope checksum, belongs to another layer, is newer than this build, or holds unparsable JSON.
inline ProfileLoadResult loadProfile(const std::filesystem::path& path, render::RenderTier platformTier) {
    const std::filesystem::path candidates[] = {path, core::SaveFile::withSuffix(path, ".bak1"),
                                                core::SaveFile::withSuffix(path, ".bak2")};
    for (int i = 0; i < 3; ++i) {
        std::ifstream f(candidates[i], std::ios::binary);
        if (!f) continue;
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                              std::istreambuf_iterator<char>());
        const auto e = core::SaveFile::decode(bytes);
        if (!e || e->layer != core::SaveLayer::Profile || e->schemaVersion > kProfileSchemaVersion)
            continue;
        const std::string_view text(reinterpret_cast<const char*>(e->payload.data()), e->payload.size());
        bool repaired = false;
        if (auto p = profileFromText(text, platformTier, &repaired)) return {std::move(*p), i, repaired};
    }
    return {defaultProfile(platformTier), -1, false};
}

} // namespace lt::game

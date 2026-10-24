#pragma once
// Legend Engine T - Section 21 item records: decodes weapon records from data/items/*.json and
// mods/<id>/items.json (Section 25) into WeaponDef. Decoding is strict so the content validator can
// point at the bad record: unknown keys, families, channels or socket slots, wrong types, negative or
// non-finite numbers and duplicate ids are reported as "source: record N (id): message".
// "timing" is optional; without it a record inherits its family archetype's moves (tcombat/Arsenal.h).
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tcombat/Arsenal.h"
#include "tcombat/Weapon.h"
#include "tcore/Json.h"
#include "tcore/JsonParser.h"

namespace lt::combat {

inline const char* channelName(Channel c) {
    static const char* kNames[] = {"physical", "frost", "ash", "verdant", "aether", "void", "lightning", "poison", "flux"};
    const auto i = static_cast<std::size_t>(c);
    return i < kChannelCount ? kNames[i] : "";
}

inline std::optional<Channel> channelFromName(std::string_view n) {
    for (std::size_t i = 0; i < kChannelCount; ++i)
        if (n == channelName(static_cast<Channel>(i))) return static_cast<Channel>(i);
    return std::nullopt;
}

// Section 7 attachment slots a weapon may expose as sockets.
inline bool isSocketSlot(std::string_view s) {
    static constexpr std::string_view kSlots[] = {"optic", "rune",   "core",      "grip", "magazine",
                                                  "guard", "muzzle", "capacitor", "lens"};
    for (std::string_view k : kSlots)
        if (s == k) return true;
    return false;
}

namespace detail {

inline constexpr std::string_view kWeaponRecordKeys[] = {"id",       "display_name", "family",  "damage",
                                                         "posture",  "vis_cost",     "weight",  "sockets",
                                                         "moveset",  "aspect_tags",  "lore_key", "timing"};

inline bool recordFail(std::string& error, std::string msg) {
    error = std::move(msg);
    return false;
}

inline bool readText(const core::Json& j, std::string_view key, std::string& out, std::string& error) {
    const core::Json& v = j[key];
    if (!v.isString() || v.str().empty())
        return recordFail(error, "'" + std::string(key) + "' must be a non-empty string");
    out = v.str();
    return true;
}

inline bool readAmount(const core::Json& v, std::string_view what, float& out, std::string& error) {
    if (!v.isNumber() || !std::isfinite(v.asNumber()) || v.asNumber() < 0.0)
        return recordFail(error, std::string(what) + " must be a non-negative number");
    out = static_cast<float>(v.asNumber());
    return true;
}

inline bool readList(const core::Json& j, std::string_view key, std::vector<std::string>& out, std::string& error) {
    const core::Json& v = j[key];
    if (!v.isArray()) return recordFail(error, "'" + std::string(key) + "' must be an array of strings");
    for (const core::Json& e : v.items()) {
        if (!e.isString() || e.str().empty())
            return recordFail(error, "'" + std::string(key) + "' must only hold non-empty strings");
        out.push_back(e.str());
    }
    return true;
}

inline bool readMs(const core::Json& t, std::string_view key, std::uint32_t& out, std::string& error) {
    const core::Json& v = t[key];
    if (!v.isInteger() || v.asInt() < 0 || v.asInt() > 60000)
        return recordFail(error, "timing '" + std::string(key) + "' must be an integer from 0 to 60000 ms");
    out = static_cast<std::uint32_t>(v.asInt());
    return true;
}

}  // namespace detail

// Copies the moves of the family's archetype record in fullArsenal(); false if the family has none.
inline bool applyArchetypeMoves(WeaponDef& d) {
    for (const WeaponDef& w : fullArsenal()) {
        if (w.family == d.family) {
            d.moves = w.moves;
            return true;
        }
    }
    return false;
}

// Decodes one Section 21 weapon record. On failure returns false, leaves `out` untouched and sets a
// record-relative `error`.
inline bool decodeWeapon(const core::Json& j, WeaponDef& out, std::string& error) {
    using detail::recordFail;
    if (!j.isObject()) return recordFail(error, "record must be an object, not " + std::string(j.typeName()));
    for (const std::string& k : j.keys()) {
        bool known = false;
        for (std::string_view w : detail::kWeaponRecordKeys) known = known || k == w;
        if (!known) return recordFail(error, "unknown key '" + k + "'");
    }
    WeaponDef d;
    std::string family;
    if (!detail::readText(j, "id", d.id, error) || !detail::readText(j, "display_name", d.displayName, error) ||
        !detail::readText(j, "family", family, error))
        return false;
    const std::optional<Family> fam = familyFromName(family);
    if (!fam) return recordFail(error, "unknown family '" + family + "'");
    d.family = *fam;

    const core::Json& dmg = j["damage"];
    if (!dmg.isObject() || dmg.empty())
        return recordFail(error, "'damage' must be a non-empty object of channel amounts");
    for (std::size_t i = 0; i < dmg.size(); ++i) {
        const std::string& key = dmg.keys()[i];
        const std::optional<Channel> ch = channelFromName(key);
        if (!ch) return recordFail(error, "unknown damage channel '" + key + "'");
        if (!detail::readAmount(dmg.items()[i], "damage '" + key + "'", d.damage[static_cast<std::size_t>(*ch)], error))
            return false;
    }
    if (!detail::readAmount(j["posture"], "'posture'", d.posture, error) ||
        !detail::readAmount(j["vis_cost"], "'vis_cost'", d.visCost, error) ||
        !detail::readAmount(j["weight"], "'weight'", d.weight, error))
        return false;

    if (!detail::readList(j, "sockets", d.sockets, error)) return false;
    if (d.sockets.size() > kMaxAttachments) return recordFail(error, "'sockets' allows at most 4 slots");
    for (std::size_t i = 0; i < d.sockets.size(); ++i) {
        if (!isSocketSlot(d.sockets[i])) return recordFail(error, "unknown socket slot '" + d.sockets[i] + "'");
        for (std::size_t k = 0; k < i; ++k)
            if (d.sockets[k] == d.sockets[i]) return recordFail(error, "duplicate socket slot '" + d.sockets[i] + "'");
    }
    if (!detail::readText(j, "moveset", d.moveset, error) || !detail::readList(j, "aspect_tags", d.aspectTags, error) ||
        !detail::readText(j, "lore_key", d.loreKey, error))
        return false;

    if (const core::Json* t = j.find("timing")) {
        if (!t->isObject()) return recordFail(error, "'timing' must be an object");
        static constexpr std::string_view kTimingKeys[] = {"windup_ms", "active_ms", "recovery_ms", "stamina"};
        for (const std::string& k : t->keys()) {
            bool known = false;
            for (std::string_view w : kTimingKeys) known = known || k == w;
            if (!known) return recordFail(error, "unknown timing key '" + k + "'");
        }
        std::uint32_t windup = 0, active = 0, recovery = 0;
        float stamina = 0.0f;
        if (!detail::readMs(*t, "windup_ms", windup, error) || !detail::readMs(*t, "active_ms", active, error) ||
            !detail::readMs(*t, "recovery_ms", recovery, error) ||
            !detail::readAmount((*t)["stamina"], "timing 'stamina'", stamina, error))
            return false;
        if (active == 0) return recordFail(error, "timing 'active_ms' must be positive (contact window)");
        d.moves = makeMoves(windup, active, recovery, stamina);
    } else if (!applyArchetypeMoves(d)) {
        return recordFail(error, "no 'timing' and no archetype moves for family '" + family + "'");
    }
    out = std::move(d);
    return true;
}

struct WeaponLoadResult {
    std::vector<WeaponDef> weapons;
    std::string error;  // empty on success: "source: record N (id): message" or "source:line:col: message"

    bool ok() const noexcept { return error.empty(); }
    explicit operator bool() const noexcept { return ok(); }
};

// Decodes an items document: a top-level array of weapon records, numbered from 1 in file order.
// Stops at the first bad record so the error names it; duplicate ids in one document are rejected.
// On failure `weapons` is empty, so a broken file never half-loads.
inline WeaponLoadResult decodeWeaponRecords(const core::Json& doc, std::string_view source = "items") {
    WeaponLoadResult r;
    if (!doc.isArray()) {
        r.error = std::string(source) + ": top level must be an array of weapon records, not " + doc.typeName();
        return r;
    }
    for (std::size_t i = 0; i < doc.size(); ++i) {
        const core::Json& rec = doc[i];
        std::string label = std::string(source) + ": record " + std::to_string(i + 1);
        if (rec.isObject() && rec["id"].isString() && !rec["id"].str().empty()) label += " (" + rec["id"].str() + ")";
        WeaponDef d;
        std::string error;
        bool good = decodeWeapon(rec, d, error);
        for (std::size_t k = 0; good && k < r.weapons.size(); ++k) {
            if (r.weapons[k].id == d.id) {
                error = "duplicate id '" + d.id + "' (first defined in record " + std::to_string(k + 1) + ")";
                good = false;
            }
        }
        if (!good) {
            r.weapons.clear();
            r.error = label + ": " + error;
            return r;
        }
        r.weapons.push_back(std::move(d));
    }
    return r;
}

// Parses JSON text and decodes it; parse errors keep their line and column.
inline WeaponLoadResult parseWeaponRecords(std::string_view text, std::string_view source = "items") {
    const core::JsonParseResult p = core::parseJson(text);
    if (!p.ok()) {
        WeaponLoadResult r;
        r.error = p.describe(source);
        return r;
    }
    return decodeWeaponRecords(p.value, source);
}

// Loads data/items/*.json or mods/<id>/items.json; the path is the error source.
inline WeaponLoadResult loadWeaponFile(const std::filesystem::path& path) {
    const std::string source = path.generic_string();
    const core::JsonParseResult p = core::loadJsonFile(path);
    if (!p.ok()) {
        WeaponLoadResult r;
        r.error = p.describe(source);
        return r;
    }
    return decodeWeaponRecords(p.value, source);
}

}  // namespace lt::combat

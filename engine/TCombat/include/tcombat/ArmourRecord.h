#pragma once
// Legend Engine T - Section 21 armour records: decodes data/armor/*.json and mods/<id>/armor.json
// (Section 25) into ArmourDef, as strictly as the weapon and enemy records: unknown keys, slots,
// channels or socket slots, wrong types, negative or non-finite numbers and duplicate ids are reported
// as "source: record N (id): message". Resistances are per-channel multipliers; omitted ones stay neutral.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tcombat/WeaponRecord.h"
#include "tcore/Json.h"
namespace lt::combat {

// Section 12 armour slots, in the same order as the game layer's body slots (head .. cloak).
enum class ArmourSlot : std::uint8_t { Head = 0, Torso, Hands, Legs, Cloak, Count };
inline constexpr std::size_t kArmourSlotCount = static_cast<std::size_t>(ArmourSlot::Count);

inline const char* armourSlotName(ArmourSlot s) {
    static const char* kNames[kArmourSlotCount] = {"head", "torso", "hands", "legs", "cloak"};
    const auto i = static_cast<std::size_t>(s);
    return i < kArmourSlotCount ? kNames[i] : "";
}

inline std::optional<ArmourSlot> armourSlotFromName(std::string_view n) {
    for (std::size_t i = 0; i < kArmourSlotCount; ++i)
        if (n == armourSlotName(static_cast<ArmourSlot>(i))) return static_cast<ArmourSlot>(i);
    return std::nullopt;
}

// Static armour definition decoded from one record. Display text lives in localisation data.
struct ArmourDef {
    std::string id;                           // stable key, e.g. armor.frostbound_plate
    std::string displayName;
    ArmourSlot slot = ArmourSlot::Torso;
    float weight = 0.0f;                      // equip load (Section 12)
    float poise = 0.0f;                       // poise contribution while worn
    std::uint32_t durability = 0;             // 0 = indestructible
    DamageVec resistances = neutralResist();  // incoming-damage multiplier per channel
    std::vector<std::string> sockets;         // Section 7 attachment slots, at most 4
    std::vector<std::string> aspectTags;
    std::string loreKey;
};

namespace detail {
inline constexpr std::string_view kArmourRecordKeys[] = {"id",         "display_name", "slot",        "weight",
                                                         "poise",      "durability",   "resistances", "sockets",
                                                         "aspect_tags", "lore_key"};
}  // namespace detail

// Decodes one armour record; on failure returns false, leaves `out` untouched and sets `error`.
inline bool decodeArmour(const core::Json& j, ArmourDef& out, std::string& error) {
    using detail::recordFail;
    if (!j.isObject()) return recordFail(error, "record must be an object, not " + std::string(j.typeName()));
    for (const std::string& k : j.keys()) {
        bool known = false;
        for (std::string_view w : detail::kArmourRecordKeys) known = known || k == w;
        if (!known) return recordFail(error, "unknown key '" + k + "'");
    }
    ArmourDef d;
    std::string slot;
    if (!detail::readText(j, "id", d.id, error) || !detail::readText(j, "display_name", d.displayName, error) ||
        !detail::readText(j, "slot", slot, error))
        return false;
    const std::optional<ArmourSlot> s = armourSlotFromName(slot);
    if (!s) return recordFail(error, "unknown armour slot '" + slot + "'");
    d.slot = *s;
    if (!detail::readAmount(j["weight"], "'weight'", d.weight, error) ||
        !detail::readAmount(j["poise"], "'poise'", d.poise, error))
        return false;
    const core::Json& dur = j["durability"];
    if (!dur.isInteger() || dur.asInt() < 0 || dur.asInt() > 100000)
        return recordFail(error, "'durability' must be an integer from 0 to 100000 (0 = indestructible)");
    d.durability = static_cast<std::uint32_t>(dur.asInt());
    const core::Json& res = j["resistances"];
    if (!res.isObject()) return recordFail(error, "'resistances' must be an object of channel multipliers");
    for (std::size_t i = 0; i < res.size(); ++i) {
        const std::string& key = res.keys()[i];
        const std::optional<Channel> ch = channelFromName(key);
        if (!ch) return recordFail(error, "unknown resistance channel '" + key + "'");
        float& slotValue = d.resistances[static_cast<std::size_t>(*ch)];
        if (!detail::readAmount(res.items()[i], "resistance '" + key + "'", slotValue, error)) return false;
    }
    if (!detail::readList(j, "sockets", d.sockets, error)) return false;
    if (d.sockets.size() > kMaxAttachments) return recordFail(error, "'sockets' allows at most 4 slots");
    for (std::size_t i = 0; i < d.sockets.size(); ++i) {
        if (!isSocketSlot(d.sockets[i])) return recordFail(error, "unknown socket slot '" + d.sockets[i] + "'");
        for (std::size_t k = 0; k < i; ++k)
            if (d.sockets[k] == d.sockets[i]) return recordFail(error, "duplicate socket slot '" + d.sockets[i] + "'");
    }
    if (!detail::readList(j, "aspect_tags", d.aspectTags, error) || !detail::readText(j, "lore_key", d.loreKey, error))
        return false;
    out = std::move(d);
    return true;
}

struct ArmourLoadResult {
    std::vector<ArmourDef> armour;
    std::string error;  // empty on success: "source: record N (id): message" or "source:line:col: message"

    bool ok() const noexcept { return error.empty(); }
    explicit operator bool() const noexcept { return ok(); }
};

// Decodes an armour document: a top-level array of armour records, numbered from 1 in file order.
// Stops at the first bad record so the error names it; duplicate ids in one document are rejected.
// On failure `armour` is empty, so a broken file never half-loads.
inline ArmourLoadResult decodeArmourRecords(const core::Json& doc, std::string_view source = "armor") {
    ArmourLoadResult r;
    if (!doc.isArray()) {
        r.error = std::string(source) + ": top level must be an array of armour records, not " + doc.typeName();
        return r;
    }
    for (std::size_t i = 0; i < doc.size(); ++i) {
        const core::Json& rec = doc[i];
        std::string label = std::string(source) + ": record " + std::to_string(i + 1);
        if (rec.isObject() && rec["id"].isString() && !rec["id"].str().empty()) label += " (" + rec["id"].str() + ")";
        ArmourDef d;
        std::string error;
        bool good = decodeArmour(rec, d, error);
        for (std::size_t k = 0; good && k < r.armour.size(); ++k) {
            if (r.armour[k].id == d.id) {
                error = "duplicate id '" + d.id + "' (first defined in record " + std::to_string(k + 1) + ")";
                good = false;
            }
        }
        if (!good) {
            r.armour.clear();
            r.error = label + ": " + error;
            return r;
        }
        r.armour.push_back(std::move(d));
    }
    return r;
}

// Parses JSON text and decodes it; parse errors keep their line and column.
inline ArmourLoadResult parseArmourRecords(std::string_view text, std::string_view source = "armor") {
    const core::JsonParseResult p = core::parseJson(text);
    if (!p.ok()) {
        ArmourLoadResult r;
        r.error = p.describe(source);
        return r;
    }
    return decodeArmourRecords(p.value, source);
}

// Loads data/armor/*.json or mods/<id>/armor.json; the path is the error source.
inline ArmourLoadResult loadArmourFile(const std::filesystem::path& path) {
    const std::string source = path.generic_string();
    const core::JsonParseResult p = core::loadJsonFile(path);
    if (!p.ok()) {
        ArmourLoadResult r;
        r.error = p.describe(source);
        return r;
    }
    return decodeArmourRecords(p.value, source);
}

}  // namespace lt::combat

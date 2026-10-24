#pragma once
// Legend Engine T - Section 21 enemy records: decodes data/enemies/*.json and mods/<id>/enemies.json
// (Section 25) into EnemyDef (tcombat/Enemy.h). Decoding is as strict as the weapon records
// (tcombat/WeaponRecord.h): unknown keys, roles or resistance channels, wrong types, negative or
// non-finite numbers and duplicate ids are reported as "source: record N (id): message".
// "health" and "neutral_until_provoked" are optional and keep the EnemyDef defaults when omitted.
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tcombat/Enemy.h"
#include "tcombat/WeaponRecord.h"
#include "tcore/Json.h"

namespace lt::combat {

inline constexpr std::size_t kEnemyRoleCount = 9;  // EnemyRole::Brute .. EnemyRole::Titan

inline const char* enemyRoleName(EnemyRole r) {
    static const char* kNames[kEnemyRoleCount] = {"brute",    "duelist", "ambusher", "caster", "swarm",
                                                  "burrower", "neutral", "flyer",    "titan"};
    const auto i = static_cast<std::size_t>(r);
    return i < kEnemyRoleCount ? kNames[i] : "";
}

inline std::optional<EnemyRole> enemyRoleFromName(std::string_view n) {
    for (std::size_t i = 0; i < kEnemyRoleCount; ++i)
        if (n == enemyRoleName(static_cast<EnemyRole>(i))) return static_cast<EnemyRole>(i);
    return std::nullopt;
}

namespace detail {

inline constexpr std::string_view kEnemyRecordKeys[] = {"id",          "role",       "perception", "posture",
                                                        "health",      "resistances", "attack_set", "drops",
                                                        "biomes",      "event_mutations", "neutral_until_provoked"};

// Reads a string list whose entries must be unique (biome ids, event mutation names).
inline bool readUniqueList(const core::Json& j, std::string_view key, std::vector<std::string>& out, std::string& error) {
    if (!readList(j, key, out, error)) return false;
    for (std::size_t i = 0; i < out.size(); ++i)
        for (std::size_t k = 0; k < i; ++k)
            if (out[k] == out[i]) return recordFail(error, "duplicate '" + std::string(key) + "' entry '" + out[i] + "'");
    return true;
}

}  // namespace detail

// Decodes one Section 21 enemy record. On failure returns false, leaves `out` untouched and sets a
// record-relative `error`.
inline bool decodeEnemy(const core::Json& j, EnemyDef& out, std::string& error) {
    using detail::recordFail;
    if (!j.isObject()) return recordFail(error, "record must be an object, not " + std::string(j.typeName()));
    for (const std::string& k : j.keys()) {
        bool known = false;
        for (std::string_view w : detail::kEnemyRecordKeys) known = known || k == w;
        if (!known) return recordFail(error, "unknown key '" + k + "'");
    }
    EnemyDef d;
    std::string role;
    if (!detail::readText(j, "id", d.id, error) || !detail::readText(j, "role", role, error)) return false;
    const std::optional<EnemyRole> r = enemyRoleFromName(role);
    if (!r) return recordFail(error, "unknown role '" + role + "'");
    d.role = *r;
    if (!detail::readAmount(j["perception"], "'perception'", d.perception, error) ||
        !detail::readAmount(j["posture"], "'posture'", d.posture, error))
        return false;
    if (const core::Json* h = j.find("health")) {
        if (!h->isNumber() || !std::isfinite(h->asNumber()) || h->asNumber() <= 0.0)
            return recordFail(error, "'health' must be a positive number");
        d.health = static_cast<float>(h->asNumber());
    }

    // Resistances are per-channel damage multipliers; channels left out stay neutral.
    const core::Json& res = j["resistances"];
    if (!res.isObject()) return recordFail(error, "'resistances' must be an object of channel multipliers");
    for (std::size_t i = 0; i < res.size(); ++i) {
        const std::string& key = res.keys()[i];
        const std::optional<Channel> ch = channelFromName(key);
        if (!ch) return recordFail(error, "unknown resistance channel '" + key + "'");
        float& slot = d.resistances[static_cast<std::size_t>(*ch)];
        if (!detail::readAmount(res.items()[i], "resistance '" + key + "'", slot, error)) return false;
    }

    if (!detail::readText(j, "attack_set", d.attackSet, error) || !detail::readList(j, "drops", d.drops, error) ||
        !detail::readUniqueList(j, "biomes", d.biomes, error) ||
        !detail::readUniqueList(j, "event_mutations", d.eventMutations, error))
        return false;
    if (d.biomes.empty()) return recordFail(error, "'biomes' must name at least one biome");

    if (const core::Json* n = j.find("neutral_until_provoked")) {
        if (!n->isBool()) return recordFail(error, "'neutral_until_provoked' must be true or false");
        d.neutralUntilProvoked = n->asBool();
    }
    out = std::move(d);
    return true;
}

struct EnemyLoadResult {
    std::vector<EnemyDef> enemies;
    std::string error;  // empty on success: "source: record N (id): message" or "source:line:col: message"

    bool ok() const noexcept { return error.empty(); }
    explicit operator bool() const noexcept { return ok(); }
};

// Decodes an enemies document: a top-level array of enemy records, numbered from 1 in file order.
// Stops at the first bad record so the error names it; duplicate ids in one document are rejected.
// On failure `enemies` is empty, so a broken file never half-loads.
inline EnemyLoadResult decodeEnemyRecords(const core::Json& doc, std::string_view source = "enemies") {
    EnemyLoadResult r;
    if (!doc.isArray()) {
        r.error = std::string(source) + ": top level must be an array of enemy records, not " + doc.typeName();
        return r;
    }
    for (std::size_t i = 0; i < doc.size(); ++i) {
        const core::Json& rec = doc[i];
        std::string label = std::string(source) + ": record " + std::to_string(i + 1);
        if (rec.isObject() && rec["id"].isString() && !rec["id"].str().empty()) label += " (" + rec["id"].str() + ")";
        EnemyDef d;
        std::string error;
        bool good = decodeEnemy(rec, d, error);
        for (std::size_t k = 0; good && k < r.enemies.size(); ++k) {
            if (r.enemies[k].id == d.id) {
                error = "duplicate id '" + d.id + "' (first defined in record " + std::to_string(k + 1) + ")";
                good = false;
            }
        }
        if (!good) {
            r.enemies.clear();
            r.error = label + ": " + error;
            return r;
        }
        r.enemies.push_back(std::move(d));
    }
    return r;
}

// Parses JSON text and decodes it; parse errors keep their line and column.
inline EnemyLoadResult parseEnemyRecords(std::string_view text, std::string_view source = "enemies") {
    const core::JsonParseResult p = core::parseJson(text);
    if (!p.ok()) {
        EnemyLoadResult r;
        r.error = p.describe(source);
        return r;
    }
    return decodeEnemyRecords(p.value, source);
}

// Loads data/enemies/*.json or mods/<id>/enemies.json; the path is the error source.
inline EnemyLoadResult loadEnemyFile(const std::filesystem::path& path) {
    const std::string source = path.generic_string();
    const core::JsonParseResult p = core::loadJsonFile(path);
    if (!p.ok()) {
        EnemyLoadResult r;
        r.error = p.describe(source);
        return r;
    }
    return decodeEnemyRecords(p.value, source);
}

}  // namespace lt::combat

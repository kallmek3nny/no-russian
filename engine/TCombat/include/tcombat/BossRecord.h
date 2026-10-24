#pragma once
// Legend Engine T - boss records (Sections 10, 14, 21): decodes data/bosses/*.json and
// mods/<id>/bosses.json (Section 25) into BossDef (tcombat/Boss.h), as strictly as the weapon, armour
// and enemy records. Unknown keys, wrong types, non-positive pools, unordered phase thresholds,
// duplicate piece or attack ids, unreachable phases and attacks gated on a missing armour piece are
// reported as "source: record N (id): message". Every phase must keep an attack that needs no armour,
// so breaking every piece (Section 10, The Frost Warden) never leaves the boss without a move.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tcombat/Boss.h"
#include "tcombat/WeaponRecord.h"
#include "tcore/Json.h"
#include "tcore/JsonParser.h"

namespace lt::combat {

namespace detail {

inline constexpr std::string_view kBossRecordKeys[] = {"id",      "display_name",     "health", "posture",
                                                       "phase_thresholds", "armour", "attacks"};
inline constexpr std::string_view kBossArmourKeys[] = {"id", "integrity", "opens_route"};
inline constexpr std::string_view kBossAttackKeys[] = {"id",     "windup_ms", "active_ms",      "recovery_ms",
                                                       "damage", "min_phase", "requires_armour"};

template <std::size_t N>
inline bool bossKeysKnown(const core::Json& j, const std::string_view (&names)[N], std::string& error) {
    for (const std::string& k : j.keys()) {
        bool known = false;
        for (std::string_view w : names) known = known || k == w;
        if (!known) return recordFail(error, "unknown key '" + k + "'");
    }
    return true;
}

// Health, posture and armour integrity are pools: finite and strictly positive.
inline bool readPositive(const core::Json& v, std::string_view what, float& out, std::string& error) {
    if (!readAmount(v, what, out, error)) return false;
    if (out <= 0.0f) return recordFail(error, std::string(what) + " must be positive");
    return true;
}

inline bool readAttackMs(const core::Json& j, std::string_view key, std::uint32_t& out, std::string& error) {
    const core::Json& v = j[key];
    if (!v.isInteger() || v.asInt() < 0 || v.asInt() > 60000)
        return recordFail(error, "'" + std::string(key) + "' must be an integer from 0 to 60000 ms");
    out = static_cast<std::uint32_t>(v.asInt());
    return true;
}

// One breakable piece: {"id", "integrity", "opens_route"}. Pieces start intact (integrity = max).
inline bool decodeBossArmour(const core::Json& j, ArmourPiece& out, std::string& error) {
    if (!j.isObject()) return recordFail(error, "must be an object, not " + std::string(j.typeName()));
    if (!bossKeysKnown(j, kBossArmourKeys, error)) return false;
    ArmourPiece a;
    if (!readText(j, "id", a.id, error) || !readPositive(j["integrity"], "'integrity'", a.maxIntegrity, error) ||
        !readText(j, "opens_route", a.opensRoute, error))
        return false;
    a.integrity = a.maxIntegrity;
    out = std::move(a);
    return true;
}

// One attack; `phaseCount` bounds "min_phase". "requires_armour" is optional (absent = always usable).
inline bool decodeBossAttack(const core::Json& j, int phaseCount, BossAttack& out, std::string& error) {
    if (!j.isObject()) return recordFail(error, "must be an object, not " + std::string(j.typeName()));
    if (!bossKeysKnown(j, kBossAttackKeys, error)) return false;
    BossAttack a;
    if (!readText(j, "id", a.id, error) || !readAttackMs(j, "windup_ms", a.windupMs, error) ||
        !readAttackMs(j, "active_ms", a.activeMs, error) || !readAttackMs(j, "recovery_ms", a.recoveryMs, error) ||
        !readAmount(j["damage"], "'damage'", a.damage, error))
        return false;
    if (a.activeMs == 0) return recordFail(error, "'active_ms' must be positive (contact window)");
    const core::Json& mp = j["min_phase"];
    if (!mp.isInteger() || mp.asInt() < 1 || mp.asInt() > phaseCount)
        return recordFail(error, "'min_phase' must be an integer from 1 to " + std::to_string(phaseCount));
    a.minPhase = static_cast<int>(mp.asInt());
    if (const core::Json* req = j.find("requires_armour")) {
        if (!req->isString() || req->str().empty())
            return recordFail(error, "'requires_armour' must be a non-empty string");
        a.requiresArmour = req->str();
    }
    out = std::move(a);
    return true;
}

}  // namespace detail

// Decodes one Section 10 boss record. On failure returns false, leaves `out` untouched and sets a
// record-relative `error`; armour piece and attack errors name their 1-based position in the record.
inline bool decodeBoss(const core::Json& j, BossDef& out, std::string& error) {
    using detail::recordFail;
    if (!j.isObject()) return recordFail(error, "record must be an object, not " + std::string(j.typeName()));
    if (!detail::bossKeysKnown(j, detail::kBossRecordKeys, error)) return false;
    BossDef d;
    if (!detail::readText(j, "id", d.id, error) || !detail::readText(j, "display_name", d.displayName, error) ||
        !detail::readPositive(j["health"], "'health'", d.maxHealth, error) ||
        !detail::readPositive(j["posture"], "'posture'", d.maxPosture, error))
        return false;

    // Phase thresholds are strictly descending health fractions inside (0, 1): 1 or more would skip phase 1
    // at full health, 0 or less is only reached by a dead boss, and a non-descending one can never be entered.
    const core::Json& th = j["phase_thresholds"];
    if (!th.isArray()) return recordFail(error, "'phase_thresholds' must be an array of health fractions");
    for (std::size_t i = 0; i < th.size(); ++i) {
        const float f = static_cast<float>(th[i].asNumber(-1.0));
        if (!th[i].isNumber() || !(f > 0.0f && f < 1.0f))
            return recordFail(error, "'phase_thresholds' entries must be numbers strictly between 0 and 1");
        if (!d.phaseThresholds.empty() && f >= d.phaseThresholds.back())
            return recordFail(error, "'phase_thresholds' must be strictly descending (phase " +
                                         std::to_string(i + 2) + " is unreachable)");
        d.phaseThresholds.push_back(f);
    }
    const int phaseCount = static_cast<int>(d.phaseThresholds.size()) + 1;

    const core::Json& armour = j["armour"];
    if (!armour.isArray()) return recordFail(error, "'armour' must be an array of breakable pieces");
    for (std::size_t i = 0; i < armour.size(); ++i) {
        ArmourPiece a;
        if (!detail::decodeBossArmour(armour[i], a, error))
            return recordFail(error, "armour piece " + std::to_string(i + 1) + ": " + error);
        for (const ArmourPiece& prev : d.armour)
            if (prev.id == a.id) return recordFail(error, "duplicate armour piece id '" + a.id + "'");
        d.armour.push_back(std::move(a));
    }

    const core::Json& attacks = j["attacks"];
    if (!attacks.isArray() || attacks.empty())
        return recordFail(error, "'attacks' must be a non-empty array of attacks");
    for (std::size_t i = 0; i < attacks.size(); ++i) {
        BossAttack a;
        if (!detail::decodeBossAttack(attacks[i], phaseCount, a, error))
            return recordFail(error, "attack " + std::to_string(i + 1) + ": " + error);
        for (const BossAttack& prev : d.attacks)
            if (prev.id == a.id) return recordFail(error, "duplicate attack id '" + a.id + "'");
        bool gateKnown = a.requiresArmour.empty();
        for (const ArmourPiece& p : d.armour) gateKnown = gateKnown || p.id == a.requiresArmour;
        if (!gateKnown)
            return recordFail(error, "attack '" + a.id + "' requires unknown armour piece '" + a.requiresArmour + "'");
        d.attacks.push_back(std::move(a));
    }

    // An ungated attack usable from phase 1 stays available in every later phase, so breaking every piece
    // never leaves the boss without a move.
    bool fallback = false;
    for (const BossAttack& a : d.attacks) fallback = fallback || (a.minPhase == 1 && a.requiresArmour.empty());
    if (!fallback)
        return recordFail(error, "needs a phase 1 attack without 'requires_armour' so every phase keeps a move "
                                 "once all armour is broken");
    out = std::move(d);
    return true;
}

struct BossLoadResult {
    std::vector<BossDef> bosses;
    std::string error;  // empty on success: "source: record N (id): message" or "source:line:col: message"

    bool ok() const noexcept { return error.empty(); }
    explicit operator bool() const noexcept { return ok(); }
};

// Decodes a boss document: a top-level array of boss records, numbered from 1 in file order.
// Stops at the first bad record so the error names it; duplicate ids in one document are rejected.
// On failure `bosses` is empty, so a broken file never half-loads.
inline BossLoadResult decodeBossRecords(const core::Json& doc, std::string_view source = "bosses") {
    BossLoadResult r;
    if (!doc.isArray()) {
        r.error = std::string(source) + ": top level must be an array of boss records, not " + doc.typeName();
        return r;
    }
    for (std::size_t i = 0; i < doc.size(); ++i) {
        const core::Json& rec = doc[i];
        std::string label = std::string(source) + ": record " + std::to_string(i + 1);
        if (rec.isObject() && rec["id"].isString() && !rec["id"].str().empty()) label += " (" + rec["id"].str() + ")";
        BossDef d;
        std::string error;
        bool good = decodeBoss(rec, d, error);
        for (std::size_t k = 0; good && k < r.bosses.size(); ++k) {
            if (r.bosses[k].id == d.id) {
                error = "duplicate id '" + d.id + "' (first defined in record " + std::to_string(k + 1) + ")";
                good = false;
            }
        }
        if (!good) {
            r.bosses.clear();
            r.error = label + ": " + error;
            return r;
        }
        r.bosses.push_back(std::move(d));
    }
    return r;
}

// Parses JSON text and decodes it; parse errors keep their line and column.
inline BossLoadResult parseBossRecords(std::string_view text, std::string_view source = "bosses") {
    const core::JsonParseResult p = core::parseJson(text);
    if (!p.ok()) {
        BossLoadResult r;
        r.error = p.describe(source);
        return r;
    }
    return decodeBossRecords(p.value, source);
}

// Loads data/bosses/*.json or mods/<id>/bosses.json; the path is the error source.
inline BossLoadResult loadBossFile(const std::filesystem::path& path) {
    const std::string source = path.generic_string();
    const core::JsonParseResult p = core::loadJsonFile(path);
    if (!p.ok()) {
        BossLoadResult r;
        r.error = p.describe(source);
        return r;
    }
    return decodeBossRecords(p.value, source);
}

}  // namespace lt::combat

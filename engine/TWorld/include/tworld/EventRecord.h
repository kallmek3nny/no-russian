#pragma once
// Legend Engine T - Section 21 event records: decodes data/events/*.json and mods/<id>/events.json
// (Section 25) into EventDef (tworld/Event.h), as strictly as the weapon and enemy records. Errors read
// "source: record N (id): message". Stages must open with "rumour" and close with "aftermath" so each
// template maps onto the runtime lifecycle; omitted "requirements" keys mean the event fits anywhere.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tcore/Json.h"
#include "tcore/JsonParser.h"
#include "tworld/Event.h"

namespace lt::world {

inline constexpr std::int64_t kMaxEventCooldownHours = 24 * 365;  // one in-game year

namespace detail {
inline constexpr std::string_view kEventRecordKeys[] = {"id", "requirements", "stages", "failure_result",
                                                        "success_result", "cooldown_hours"};
inline constexpr std::string_view kEventRequirementKeys[] = {"biome", "faction"};

inline bool eventRecordFail(std::string& error, std::string msg) {
    error = std::move(msg);
    return false;
}
template <std::size_t N>
inline bool eventRecordKeysKnown(const core::Json& j, const std::string_view (&names)[N], std::string_view where,
                                 std::string& error) {
    for (const std::string& k : j.keys()) {
        bool known = false;
        for (std::string_view n : names) known = known || k == n;
        if (!known) return eventRecordFail(error, "unknown " + std::string(where) + "key '" + k + "'");
    }
    return true;
}

inline bool eventRecordText(const core::Json& v, std::string_view what, std::string& out, std::string& error) {
    if (!v.isString() || v.str().empty())
        return eventRecordFail(error, std::string(what) + " must be a non-empty string");
    out = v.str();
    return true;
}

// A list of non-empty strings with no repeats (biome ids, stage names).
inline bool eventRecordList(const core::Json& v, std::string_view what, std::vector<std::string>& out,
                            std::string& error) {
    if (!v.isArray()) return eventRecordFail(error, std::string(what) + " must be an array of strings");
    std::vector<std::string> list;
    for (const core::Json& e : v.items()) {
        if (!e.isString() || e.str().empty())
            return eventRecordFail(error, std::string(what) + " must only hold non-empty strings");
        for (const std::string& seen : list)
            if (seen == e.str())
                return eventRecordFail(error, "duplicate " + std::string(what) + " entry '" + seen + "'");
        list.push_back(e.str());
    }
    out = std::move(list);
    return true;
}
}  // namespace detail

// Decodes one Section 21 event record. On failure returns false, leaves `out` untouched and sets a
// record-relative `error`.
inline bool decodeEvent(const core::Json& j, EventDef& out, std::string& error) {
    using detail::eventRecordFail;
    if (!j.isObject()) return eventRecordFail(error, "record must be an object, not " + std::string(j.typeName()));
    if (!detail::eventRecordKeysKnown(j, detail::kEventRecordKeys, "", error)) return false;
    EventDef d;
    if (!detail::eventRecordText(j["id"], "'id'", d.id, error)) return false;
    const core::Json& req = j["requirements"];
    if (!req.isObject()) return eventRecordFail(error, "'requirements' must be an object (use {} for anywhere)");
    if (!detail::eventRecordKeysKnown(req, detail::kEventRequirementKeys, "requirements ", error)) return false;
    if (const core::Json* b = req.find("biome")) {
        if (!detail::eventRecordList(*b, "'requirements.biome'", d.biomes, error)) return false;
        if (d.biomes.empty())
            return eventRecordFail(error, "'requirements.biome' must name a biome; omit it for every biome");
    }
    if (const core::Json* f = req.find("faction"))
        if (!detail::eventRecordText(*f, "'requirements.faction'", d.faction, error)) return false;
    if (!detail::eventRecordList(j["stages"], "'stages'", d.stages, error)) return false;
    if (d.stages.size() < 2 || d.stages.front() != toString(EventState::Rumour) ||
        d.stages.back() != toString(EventState::Aftermath))
        return eventRecordFail(error, "'stages' must open with \"rumour\" and close with \"aftermath\"");
    if (!detail::eventRecordText(j["failure_result"], "'failure_result'", d.failureResult, error) ||
        !detail::eventRecordText(j["success_result"], "'success_result'", d.successResult, error))
        return false;
    const core::Json& cd = j["cooldown_hours"];
    if (!cd.isInteger() || cd.asInt(-1) < 0 || cd.asInt(-1) > kMaxEventCooldownHours)
        return eventRecordFail(error, "'cooldown_hours' must be an integer from 0 to " +
                                          std::to_string(kMaxEventCooldownHours));
    d.cooldownHours = static_cast<std::uint32_t>(cd.asInt());
    out = std::move(d);
    return true;
}

struct EventLoadResult {
    std::vector<EventDef> events;
    std::string error;  // empty on success: "source: record N (id): message" or "source:line:col: message"

    bool ok() const noexcept { return error.empty(); }
    explicit operator bool() const noexcept { return ok(); }
};

// Decodes an events document: a top-level array of event records, numbered from 1 in file order.
// Stops at the first bad record so the error names it; duplicate ids in one document are rejected.
// On failure `events` is empty, so a broken file never half-loads.
inline EventLoadResult decodeEventRecords(const core::Json& doc, std::string_view source = "events") {
    EventLoadResult r;
    if (!doc.isArray()) {
        r.error = std::string(source) + ": top level must be an array of event records, not " + doc.typeName();
        return r;
    }
    for (std::size_t i = 0; i < doc.size(); ++i) {
        const core::Json& rec = doc[i];
        std::string label = std::string(source) + ": record " + std::to_string(i + 1);
        if (rec.isObject() && rec["id"].isString() && !rec["id"].str().empty()) label += " (" + rec["id"].str() + ")";
        EventDef d;
        std::string error;
        bool good = decodeEvent(rec, d, error);
        for (std::size_t k = 0; good && k < r.events.size(); ++k) {
            if (r.events[k].id == d.id) {
                error = "duplicate id '" + d.id + "' (first defined in record " + std::to_string(k + 1) + ")";
                good = false;
            }
        }
        if (!good) {
            r.events.clear();
            r.error = label + ": " + error;
            return r;
        }
        r.events.push_back(std::move(d));
    }
    return r;
}

// Parses JSON text and decodes it; parse errors keep their line and column.
inline EventLoadResult parseEventRecords(std::string_view text, std::string_view source = "events") {
    const core::JsonParseResult p = core::parseJson(text);
    if (!p.ok()) {
        EventLoadResult r;
        r.error = p.describe(source);
        return r;
    }
    return decodeEventRecords(p.value, source);
}

// Loads data/events/*.json or mods/<id>/events.json; the path is the error source.
inline EventLoadResult loadEventFile(const std::filesystem::path& path) {
    const std::string source = path.generic_string();
    const core::JsonParseResult p = core::loadJsonFile(path);
    if (!p.ok()) {
        EventLoadResult r;
        r.error = p.describe(source);
        return r;
    }
    return decodeEventRecords(p.value, source);
}

}  // namespace lt::world

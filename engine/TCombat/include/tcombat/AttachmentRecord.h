#pragma once
// Legend Engine T - Section 7/21 attachment records: decodes data/attachments/*.json and
// mods/<id>/attachments.json (Section 25) into AttachmentDef, as strictly as the weapon and armour
// records. An attachment fits one Section 7 socket slot, may restrict its host families, and changes
// damage, posture, range, noise, elemental response and flux generation; omitted modifiers stay
// neutral. Infusion recipe outputs (Section 8) such as attachment.aether_core resolve against these ids.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tcombat/Arsenal.h"
#include "tcombat/WeaponRecord.h"
#include "tcore/Json.h"
#include "tcore/JsonParser.h"

namespace lt::combat {

// Combat modifiers (installAttachments) plus the item metadata the inventory needs (Section 12).
struct AttachmentDef {
    Attachment mods;               // id, slot, compatible families and stat changes
    std::string displayName;
    float weight = 0.0f;
    std::uint32_t durability = 0;  // 0 = indestructible
    std::vector<std::string> aspectTags;
    std::string loreKey;
};

namespace detail {

inline constexpr std::string_view kAttachmentRecordKeys[] = {
    "id",           "display_name", "slot",         "compatible",    "weight",      "durability",
    "aspect_tags",  "lore_key",     "damage_scale", "posture_scale", "range_scale", "noise",
    "flux_per_hit", "add_damage",   "visible_on_model"};

// Optional multiplier: absent keeps 1.0; present must be a finite number from 0 to 10.
inline bool readScale(const core::Json& j, std::string_view key, float& out, std::string& error) {
    const core::Json* v = j.find(key);
    if (v == nullptr) return true;
    if (!v->isNumber() || !std::isfinite(v->asNumber()) || v->asNumber() < 0.0 || v->asNumber() > 10.0)
        return recordFail(error, "'" + std::string(key) + "' must be a number from 0 to 10");
    out = static_cast<float>(v->asNumber());
    return true;
}

// "compatible" is optional; each name must be a Section 7 family, listed once. Empty = any host family.
inline bool readCompatible(const core::Json& j, std::vector<Family>& out, std::string& error) {
    std::vector<std::string> names;
    if (j.contains("compatible") && !readList(j, "compatible", names, error)) return false;
    for (const std::string& n : names) {
        const std::optional<Family> fam = familyFromName(n);
        if (!fam) return recordFail(error, "unknown compatible family '" + n + "'");
        if (std::find(out.begin(), out.end(), *fam) != out.end())
            return recordFail(error, "duplicate compatible family '" + n + "'");
        out.push_back(*fam);
    }
    return true;
}

// Optional stat modifiers; every omitted one keeps the neutral combat::Attachment default.
inline bool readModifiers(const core::Json& j, Attachment& a, std::string& error) {
    if (!readScale(j, "damage_scale", a.damageScale, error) || !readScale(j, "posture_scale", a.postureScale, error) ||
        !readScale(j, "range_scale", a.rangeScale, error))
        return false;
    if (const core::Json* n = j.find("noise")) {  // metres; negative values muffle (baffled muzzle)
        if (!n->isNumber() || !std::isfinite(n->asNumber()) || std::fabs(n->asNumber()) > 100.0)
            return recordFail(error, "'noise' must be a number of metres from -100 to 100");
        a.noise = static_cast<float>(n->asNumber());
    }
    if (const core::Json* f = j.find("flux_per_hit"))
        if (!readAmount(*f, "'flux_per_hit'", a.fluxPerHit, error)) return false;
    if (const core::Json* add = j.find("add_damage")) {  // one elemental channel, as in combat::Attachment
        if (!add->isObject() || add->size() != 1)
            return recordFail(error, "'add_damage' must be an object naming exactly one channel");
        const std::string& key = add->keys()[0];
        const std::optional<Channel> ch = channelFromName(key);
        if (!ch) return recordFail(error, "unknown add_damage channel '" + key + "'");
        a.addChannel = *ch;
        if (!readAmount(add->items()[0], "add_damage '" + key + "'", a.addDamage, error)) return false;
    }
    if (const core::Json* vis = j.find("visible_on_model")) {
        if (!vis->isBool()) return recordFail(error, "'visible_on_model' must be true or false");
        a.visibleOnModel = vis->asBool();
    }
    return true;
}

}  // namespace detail

// Decodes one attachment record; on failure returns false, leaves `out` untouched and sets `error`.
inline bool decodeAttachment(const core::Json& j, AttachmentDef& out, std::string& error) {
    using detail::recordFail;
    if (!j.isObject()) return recordFail(error, "record must be an object, not " + std::string(j.typeName()));
    for (const std::string& k : j.keys()) {
        bool known = false;
        for (std::string_view w : detail::kAttachmentRecordKeys) known = known || k == w;
        if (!known) return recordFail(error, "unknown key '" + k + "'");
    }
    AttachmentDef d;
    if (!detail::readText(j, "id", d.mods.id, error) || !detail::readText(j, "display_name", d.displayName, error) ||
        !detail::readText(j, "slot", d.mods.slot, error))
        return false;
    if (!isSocketSlot(d.mods.slot)) return recordFail(error, "unknown socket slot '" + d.mods.slot + "'");
    if (!detail::readCompatible(j, d.mods.compatible, error) ||
        !detail::readAmount(j["weight"], "'weight'", d.weight, error))
        return false;
    const core::Json& dur = j["durability"];
    if (!dur.isInteger() || dur.asInt() < 0 || dur.asInt() > 100000)
        return recordFail(error, "'durability' must be an integer from 0 to 100000 (0 = indestructible)");
    d.durability = static_cast<std::uint32_t>(dur.asInt());
    if (!detail::readModifiers(j, d.mods, error)) return false;
    if (!detail::readList(j, "aspect_tags", d.aspectTags, error) || !detail::readText(j, "lore_key", d.loreKey, error))
        return false;
    out = std::move(d);
    return true;
}

struct AttachmentLoadResult {
    std::vector<AttachmentDef> attachments;
    std::string error;  // empty on success: "source: record N (id): message" or "source:line:col: message"

    bool ok() const noexcept { return error.empty(); }
    explicit operator bool() const noexcept { return ok(); }
};

// Decodes an attachment document: a top-level array of attachment records, numbered from 1 in file
// order. Stops at the first bad record so the error names it; duplicate ids in one document are
// rejected. On failure `attachments` is empty, so a broken file never half-loads.
inline AttachmentLoadResult decodeAttachmentRecords(const core::Json& doc, std::string_view source = "attachments") {
    AttachmentLoadResult r;
    if (!doc.isArray()) {
        r.error = std::string(source) + ": top level must be an array of attachment records, not " + doc.typeName();
        return r;
    }
    for (std::size_t i = 0; i < doc.size(); ++i) {
        const core::Json& rec = doc[i];
        std::string label = std::string(source) + ": record " + std::to_string(i + 1);
        if (rec.isObject() && rec["id"].isString() && !rec["id"].str().empty()) label += " (" + rec["id"].str() + ")";
        AttachmentDef d;
        std::string error;
        bool good = decodeAttachment(rec, d, error);
        for (std::size_t k = 0; good && k < r.attachments.size(); ++k) {
            if (r.attachments[k].mods.id == d.mods.id) {
                error = "duplicate id '" + d.mods.id + "' (first defined in record " + std::to_string(k + 1) + ")";
                good = false;
            }
        }
        if (!good) {
            r.attachments.clear();
            r.error = label + ": " + error;
            return r;
        }
        r.attachments.push_back(std::move(d));
    }
    return r;
}

// Parses JSON text and decodes it; parse errors keep their line and column.
inline AttachmentLoadResult parseAttachmentRecords(std::string_view text, std::string_view source = "attachments") {
    const core::JsonParseResult p = core::parseJson(text);
    if (!p.ok()) {
        AttachmentLoadResult r;
        r.error = p.describe(source);
        return r;
    }
    return decodeAttachmentRecords(p.value, source);
}

// Loads data/attachments/*.json or mods/<id>/attachments.json; the path is the error source.
inline AttachmentLoadResult loadAttachmentFile(const std::filesystem::path& path) {
    const std::string source = path.generic_string();
    const core::JsonParseResult p = core::loadJsonFile(path);
    if (!p.ok()) {
        AttachmentLoadResult r;
        r.error = p.describe(source);
        return r;
    }
    return decodeAttachmentRecords(p.value, source);
}

// Combat modifiers of decoded records, ready for installAttachments and findAttachment.
inline std::vector<Attachment> attachmentMods(const std::vector<AttachmentDef>& defs) {
    std::vector<Attachment> v;
    v.reserve(defs.size());
    for (const AttachmentDef& d : defs) v.push_back(d.mods);
    return v;
}

}  // namespace lt::combat

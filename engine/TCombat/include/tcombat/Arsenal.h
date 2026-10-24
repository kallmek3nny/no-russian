#pragma once
// Legend Engine T - Milestone C: full weapon family roster and attachments (design doc sections 7, 12, 21).
// Every section 7 family gets at least one deterministic record with all seven move kinds (light, heavy,
// sprint, charged, air, guard-counter, execution). Attachments carry slot + family compatibility tags and
// change damage, posture, range, noise, elemental response and flux generation.
#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "tcombat/Weapon.h"

namespace lt::combat {

inline constexpr std::size_t kFamilyCount = static_cast<std::size_t>(Family::Count);
inline constexpr std::size_t kMaxAttachments = 4;  // section 12: four attachment slots

inline const char* familyName(Family f) {
    static const char* kNames[] = {"greatblade", "longsword", "spear",     "bow",          "axe",
                                   "hammer",     "sickle",    "crossbow",  "handcannon",   "arcane_focus",
                                   "chakram",    "gauntlet",  "shield",    "voxel_tool"};
    const auto i = static_cast<std::size_t>(f);
    return i < kFamilyCount ? kNames[i] : "";
}

inline std::optional<Family> familyFromName(const std::string& n) {
    for (std::size_t i = 0; i < kFamilyCount; ++i)
        if (n == familyName(static_cast<Family>(i))) return static_cast<Family>(i);
    return std::nullopt;
}

inline WeaponDef makeWeapon(const std::string& id, const std::string& name, Family fam, float physical,
                            Channel second, float secondDamage, float posture, float vis, float weight,
                            std::vector<std::string> sockets, const std::string& moveset,
                            std::vector<std::string> aspects, std::uint32_t windup, std::uint32_t active,
                            std::uint32_t recovery, float stamina) {
    WeaponDef d;
    d.id = id;
    d.displayName = name;
    d.family = fam;
    d.damage[static_cast<std::size_t>(Channel::Physical)] = physical;
    d.damage[static_cast<std::size_t>(second)] += secondDamage;
    d.posture = posture;
    d.visCost = vis;
    d.weight = weight;
    d.sockets = std::move(sockets);
    d.moveset = moveset;
    d.aspectTags = std::move(aspects);
    d.loreKey = "item." + (id.rfind("weapon.", 0) == 0 ? id.substr(7) : id) + ".lore";
    d.moves = makeMoves(windup, active, recovery, stamina);
    return d;
}

// Vertical-slice four plus one record for every remaining section 7 family.
inline std::vector<WeaponDef> fullArsenal() {
    std::vector<WeaponDef> w = verticalSliceWeapons();
    w.push_back(makeWeapon("weapon.ashcleaver_axe", "Ashcleaver", Family::Axe, 30, Channel::Ash, 6, 28, 5, 5.0f,
                           {"grip", "rune"}, "axe_01", {"decay"}, 380, 116, 480, 18));
    w.push_back(makeWeapon("weapon.khar_maul", "Khar-Duun Maul", Family::Hammer, 38, Channel::Lightning, 4, 45, 6,
                           8.5f, {"core", "guard"}, "hammer_01", {"form"}, 600, 150, 700, 28));
    w.push_back(makeWeapon("weapon.grove_sickle", "Grove Sickle", Family::Sickle, 18, Channel::Poison, 8, 10, 4, 1.8f,
                           {"rune"}, "sickle_01", {"life", "decay"}, 220, 83, 300, 10));
    w.push_back(makeWeapon("weapon.order_arbalest", "Order Arbalest", Family::Crossbow, 34, Channel::Physical, 0, 14,
                           4, 4.2f, {"optic", "magazine"}, "crossbow_01", {"motion"}, 500, 33, 700, 8));
    w.push_back(makeWeapon("weapon.furnace_handcannon", "Furnace Handcannon", Family::Handcannon, 40, Channel::Ash,
                           12, 30, 10, 5.5f, {"muzzle", "magazine", "capacitor"}, "handcannon_01",
                           {"motion", "decay"}, 650, 33, 900, 12));
    w.push_back(makeWeapon("weapon.memory_focus", "Memory Focus", Family::ArcaneFocus, 6, Channel::Aether, 22, 6, 12,
                           1.2f, {"core", "lens"}, "focus_01", {"memory", "light"}, 450, 66, 400, 6));
    w.push_back(makeWeapon("weapon.aether_chakram", "Aether Chakram", Family::Chakram, 16, Channel::Aether, 6, 8, 5,
                           1.5f, {"rune", "grip"}, "chakram_01", {"motion"}, 280, 100, 350, 9));
    w.push_back(makeWeapon("weapon.ashen_knuckles", "Ashen Knuckles", Family::Gauntlet, 14, Channel::Ash, 4, 12, 3,
                           1.6f, {"guard", "rune"}, "gauntlet_01", {"life"}, 160, 66, 220, 7));
    w.push_back(makeWeapon("weapon.warden_bulwark", "Warden Bulwark", Family::Shield, 12, Channel::Physical, 0, 30, 2,
                           6.0f, {"guard", "rune"}, "shield_01", {"form"}, 400, 100, 500, 14));
    w.push_back(makeWeapon("weapon.field_pick", "Field Pick", Family::VoxelTool, 14, Channel::Physical, 0, 8, 0, 2.5f,
                           {"grip"}, "voxel_tool_01", {"form"}, 350, 100, 400, 10));
    return w;
}

inline bool isRanged(Family f) {
    return f == Family::Bow || f == Family::Crossbow || f == Family::Handcannon || f == Family::ArcaneFocus ||
           f == Family::Chakram;
}

// --- Attachments (section 7) --------------------------------------------------------------

struct Attachment {
    std::string id;
    std::string slot;                 // optic, rune, core, grip, magazine, guard, muzzle, capacitor, lens
    std::vector<Family> compatible;   // empty = any family that exposes the slot
    float damageScale = 1.0f;
    float postureScale = 1.0f;
    float rangeScale = 1.0f;
    float noise = 0.0f;               // added noise radius in metres
    float fluxPerHit = 0.0f;
    Channel addChannel = Channel::Physical;
    float addDamage = 0.0f;
    bool visibleOnModel = true;
};

inline bool canInstall(const WeaponDef& w, const Attachment& a) {
    if (std::find(w.sockets.begin(), w.sockets.end(), a.slot) == w.sockets.end()) return false;
    return a.compatible.empty() ||
           std::find(a.compatible.begin(), a.compatible.end(), w.family) != a.compatible.end();
}

struct ModifiedWeapon {
    WeaponDef def;
    float rangeScale = 1.0f;
    float noise = 0.0f;
    float fluxPerHit = 0.0f;
    std::vector<std::string> installed;  // attachment ids, in install order (visible on model)
};

// Returns nullopt if any attachment is incompatible, shares a slot, or exceeds the four-slot limit.
inline std::optional<ModifiedWeapon> installAttachments(const WeaponDef& base, const std::vector<Attachment>& atts) {
    if (atts.size() > kMaxAttachments) return std::nullopt;
    ModifiedWeapon m{base, 1.0f, 0.0f, 0.0f, {}};
    std::vector<std::string> usedSlots;
    for (const auto& a : atts) {
        if (!canInstall(base, a)) return std::nullopt;
        if (std::find(usedSlots.begin(), usedSlots.end(), a.slot) != usedSlots.end()) return std::nullopt;
        usedSlots.push_back(a.slot);
        for (auto& d : m.def.damage) d *= a.damageScale;
        m.def.damage[static_cast<std::size_t>(a.addChannel)] += a.addDamage;
        m.def.posture *= a.postureScale;
        m.rangeScale *= a.rangeScale;
        m.noise += a.noise;
        m.fluxPerHit += a.fluxPerHit;
        if (a.visibleOnModel) m.installed.push_back(a.id);
    }
    return m;
}

inline std::vector<Attachment> defaultAttachments() {
    std::vector<Attachment> v;
    { Attachment a; a.id = "att.hunter_optic"; a.slot = "optic"; a.compatible = {Family::Bow, Family::Crossbow};
      a.rangeScale = 1.5f; v.push_back(a); }
    { Attachment a; a.id = "att.frost_rune"; a.slot = "rune"; a.addChannel = Channel::Frost; a.addDamage = 8;
      a.fluxPerHit = 0.5f; v.push_back(a); }
    { Attachment a; a.id = "att.taumium_core"; a.slot = "core"; a.damageScale = 1.15f; a.addChannel = Channel::Aether;
      a.addDamage = 6; a.fluxPerHit = 1.5f; v.push_back(a); }
    { Attachment a; a.id = "att.wrapped_grip"; a.slot = "grip"; a.postureScale = 1.1f; a.visibleOnModel = true;
      v.push_back(a); }
    { Attachment a; a.id = "att.drum_magazine"; a.slot = "magazine"; a.compatible = {Family::Crossbow, Family::Handcannon};
      a.damageScale = 0.9f; a.noise = 4.0f; v.push_back(a); }
    { Attachment a; a.id = "att.wardens_guard"; a.slot = "guard"; a.postureScale = 1.25f; v.push_back(a); }
    { Attachment a; a.id = "att.baffled_muzzle"; a.slot = "muzzle"; a.compatible = {Family::Handcannon};
      a.noise = -10.0f; a.damageScale = 0.95f; v.push_back(a); }
    { Attachment a; a.id = "att.back_capacitor"; a.slot = "capacitor"; a.addChannel = Channel::Lightning;
      a.addDamage = 10; a.fluxPerHit = 2.0f; v.push_back(a); }
    { Attachment a; a.id = "att.planar_lens"; a.slot = "lens"; a.compatible = {Family::ArcaneFocus};
      a.rangeScale = 1.3f; a.addChannel = Channel::Void; a.addDamage = 5; a.fluxPerHit = 1.0f; v.push_back(a); }
    return v;
}

inline std::optional<Attachment> findAttachment(const std::vector<Attachment>& set, const std::string& id) {
    for (const auto& a : set) if (a.id == id) return a;
    return std::nullopt;
}

}  // namespace lt::combat

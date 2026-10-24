// Sections 7/12/21/25 attachment record tests: a full record decodes into an AttachmentDef, omitted
// modifiers stay neutral, decoded modifiers feed installAttachments, the shipped
// data/attachments/core_set.json loads, and strict record errors (keys, slots, families, scales,
// noise, channels, duplicate ids) name the source and record for the content validator.
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "TestHarness.h"
#include "tcombat/AttachmentRecord.h"

using namespace lt::combat;

namespace {

const std::string kRecord =
    R"({"id":"attachment.test_lens","display_name":"Test Lens","slot":"lens","compatible":["arcane_focus"],)"
    R"("weight":0.25,"durability":90,"damage_scale":1.25,"posture_scale":0.5,"range_scale":1.5,"noise":-2,)"
    R"("flux_per_hit":0.75,"add_damage":{"void":5},"visible_on_model":false,"aspect_tags":["light"],)"
    R"("lore_key":"item.test_lens.lore"})";

const std::string kMinimal = R"({"id":"attachment.min","display_name":"Min","slot":"grip","weight":0,)"
                             R"("durability":0,"aspect_tags":[],"lore_key":"item.min.lore"})";

// kRecord with the first `from` replaced by `to`.
std::string with(std::string_view from, std::string_view to, std::string s = kRecord) {
    const std::size_t at = s.find(from);
    if (at != std::string::npos) s.replace(at, from.size(), to);
    return s;
}

AttachmentLoadResult load(const std::string& records) {
    return parseAttachmentRecords("[" + records + "]", "t.json");
}

bool failsWith(const std::string& records, std::string_view message) {
    const AttachmentLoadResult r = load(records);
    return !r.ok() && r.attachments.empty() && r.error.find(message) != std::string::npos;
}

}  // namespace

int main() {
    TEST_SECTION("valid record");
    {
        const AttachmentLoadResult r = load(kRecord);
        CHECK(r.ok());
        CHECK_EQ(r.attachments.size(), std::size_t(1));
        if (r.ok() && r.attachments.size() == 1) {
            const AttachmentDef& d = r.attachments[0];
            CHECK_EQ(d.mods.id, std::string("attachment.test_lens"));
            CHECK_EQ(d.displayName, std::string("Test Lens"));
            CHECK_EQ(d.mods.slot, std::string("lens"));
            CHECK(d.mods.compatible == std::vector<Family>({Family::ArcaneFocus}));
            CHECK_EQ(d.weight, 0.25f);
            CHECK_EQ(d.durability, 90u);
            CHECK_EQ(d.mods.damageScale, 1.25f);
            CHECK_EQ(d.mods.postureScale, 0.5f);
            CHECK_EQ(d.mods.rangeScale, 1.5f);
            CHECK_EQ(d.mods.noise, -2.0f);
            CHECK_EQ(d.mods.fluxPerHit, 0.75f);
            CHECK(d.mods.addChannel == Channel::Void);
            CHECK_EQ(d.mods.addDamage, 5.0f);
            CHECK(!d.mods.visibleOnModel);
            CHECK(d.aspectTags == std::vector<std::string>({"light"}));
            CHECK_EQ(d.loreKey, std::string("item.test_lens.lore"));
        }
        // Omitted modifiers keep the neutral combat::Attachment defaults.
        const AttachmentLoadResult m = load(kMinimal);
        CHECK(m.ok() && m.attachments.size() == 1);
        if (m.ok() && m.attachments.size() == 1) {
            const Attachment& a = m.attachments[0].mods;
            CHECK(a.compatible.empty());
            CHECK_EQ(a.damageScale, 1.0f);
            CHECK_EQ(a.postureScale, 1.0f);
            CHECK_EQ(a.rangeScale, 1.0f);
            CHECK_EQ(a.noise, 0.0f);
            CHECK_EQ(a.fluxPerHit, 0.0f);
            CHECK(a.addChannel == Channel::Physical);
            CHECK_EQ(a.addDamage, 0.0f);
            CHECK(a.visibleOnModel);
            CHECK_EQ(m.attachments[0].durability, 0u);
        }
        CHECK(load("").ok());
        CHECK(load("").attachments.empty());
    }

    TEST_SECTION("decoded modifiers feed installAttachments");
    {
        const AttachmentLoadResult r = load(kRecord);
        const std::vector<WeaponDef> arsenal = fullArsenal();
        const WeaponDef* focus = nullptr;  // core + lens sockets, arcane_focus family
        const WeaponDef* axe = nullptr;    // grip + rune sockets only
        for (const WeaponDef& w : arsenal) {
            if (w.id == "weapon.memory_focus") focus = &w;
            if (w.id == "weapon.ashcleaver_axe") axe = &w;
        }
        CHECK(r.ok() && r.attachments.size() == 1 && focus != nullptr && axe != nullptr);
        if (r.ok() && r.attachments.size() == 1 && focus != nullptr && axe != nullptr) {
            const std::vector<Attachment> mods = attachmentMods(r.attachments);
            CHECK_EQ(mods.size(), std::size_t(1));
            CHECK(findAttachment(mods, "attachment.test_lens").has_value());
            const std::optional<ModifiedWeapon> m = installAttachments(*focus, mods);
            CHECK(m.has_value());
            if (m) {
                const auto phys = static_cast<std::size_t>(Channel::Physical);
                const auto voidCh = static_cast<std::size_t>(Channel::Void);
                CHECK_EQ(m->def.damage[phys], focus->damage[phys] * 1.25f);
                CHECK_EQ(m->def.damage[voidCh], focus->damage[voidCh] * 1.25f + 5.0f);
                CHECK_EQ(m->def.posture, focus->posture * 0.5f);
                CHECK_EQ(m->rangeScale, 1.5f);
                CHECK_EQ(m->noise, -2.0f);
                CHECK_EQ(m->fluxPerHit, 0.75f);
                CHECK(m->installed.empty());  // visible_on_model: false keeps it off the model
            }
            // The axe exposes no lens socket and is not an arcane focus.
            CHECK(!installAttachments(*axe, mods).has_value());
        }
    }

#ifdef LT_DATA_DIR
    TEST_SECTION("shipped core set");
    {
        const std::filesystem::path dir = std::filesystem::path(LT_DATA_DIR) / "attachments";
        const AttachmentLoadResult r = loadAttachmentFile(dir / "core_set.json");
        CHECK(r.ok());
        CHECK_EQ(r.attachments.size(), std::size_t(4));
        const std::vector<Attachment> mods = attachmentMods(r.attachments);
        // attachment.aether_core is the output of infusion.aether_core in data/recipes/infusions.json.
        const std::optional<Attachment> core = findAttachment(mods, "attachment.aether_core");
        CHECK(core.has_value());
        if (core) {
            CHECK_EQ(core->slot, std::string("core"));
            CHECK_EQ(core->damageScale, 1.15f);
            CHECK(core->addChannel == Channel::Aether);
            CHECK_EQ(core->addDamage, 6.0f);
            CHECK_EQ(core->fluxPerHit, 1.5f);
        }
        const std::optional<Attachment> muzzle = findAttachment(mods, "attachment.baffled_muzzle");
        CHECK(muzzle.has_value());
        if (muzzle) {
            CHECK(muzzle->compatible == std::vector<Family>({Family::Handcannon}));
            CHECK_EQ(muzzle->noise, -10.0f);
        }
        const AttachmentLoadResult missing = loadAttachmentFile(dir / "missing.json");
        CHECK(!missing.ok() && missing.attachments.empty());
    }
#endif

    TEST_SECTION("strict record errors");
    {
        CHECK(failsWith(with("\"noise\":-2,", "\"volume\":-2,"), "unknown key 'volume'"));
        CHECK(failsWith(with("\"id\":\"attachment.test_lens\",", ""), "'id' must be a non-empty string"));
        CHECK(failsWith(with("\"slot\":\"lens\"", "\"slot\":\"barrel\""), "unknown socket slot 'barrel'"));
        CHECK(failsWith(with("[\"arcane_focus\"]", "[\"staff\"]"), "unknown compatible family 'staff'"));
        CHECK(failsWith(with("[\"arcane_focus\"]", "[\"bow\",\"bow\"]"), "duplicate compatible family 'bow'"));
        CHECK(failsWith(with("\"weight\":0.25", "\"weight\":-1"), "'weight' must be a non-negative number"));
        CHECK(failsWith(with("\"durability\":90", "\"durability\":-1"), "'durability' must be an integer"));
        CHECK(failsWith(with("\"damage_scale\":1.25", "\"damage_scale\":11"), "'damage_scale' must be a number from 0 to 10"));
        CHECK(failsWith(with("\"range_scale\":1.5", "\"range_scale\":-1"), "'range_scale' must be a number from 0 to 10"));
        CHECK(failsWith(with("\"noise\":-2", "\"noise\":-250"), "'noise' must be a number of metres from -100 to 100"));
        CHECK(failsWith(with("\"flux_per_hit\":0.75", "\"flux_per_hit\":-1"), "'flux_per_hit' must be a non-negative number"));
        CHECK(failsWith(with("{\"void\":5}", "{\"plasma\":5}"), "unknown add_damage channel 'plasma'"));
        CHECK(failsWith(with("{\"void\":5}", "{\"void\":5,\"frost\":1}"), "naming exactly one channel"));
        CHECK(failsWith(with("{\"void\":5}", "{\"void\":-5}"), "add_damage 'void' must be a non-negative number"));
        CHECK(failsWith(with("\"visible_on_model\":false", "\"visible_on_model\":\"no\""), "must be true or false"));
        CHECK(failsWith("42", "record must be an object"));
        CHECK(failsWith(kRecord + "," + kRecord, "duplicate id 'attachment.test_lens' (first defined in record 1)"));
        // Errors name the source, the 1-based record and its id; nothing half-loads.
        const AttachmentLoadResult second = load(kMinimal + "," + with("\"slot\":\"lens\"", "\"slot\":\"barrel\""));
        CHECK(!second.ok() && second.attachments.empty());
        CHECK_EQ(second.error, std::string("t.json: record 2 (attachment.test_lens): unknown socket slot 'barrel'"));
        const AttachmentLoadResult notArray = parseAttachmentRecords("{}", "t.json");
        CHECK(!notArray.ok() && notArray.error.find("t.json: top level must be an array") == 0);
        const AttachmentLoadResult broken = parseAttachmentRecords("[", "t.json");
        CHECK(!broken.ok() && broken.attachments.empty() && broken.error.rfind("t.json:", 0) == 0);
    }

    return lt::test::finish("attachment_record");
}

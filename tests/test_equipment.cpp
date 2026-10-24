// Section 12 equipment tests: slot rules, deterministic item generation, weapon
// attachments with compatibility tags, weight-driven poise/stamina/noise/dodge,
// durability, quick-use belt stacks, deterministic replay and save/reload.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "TestHarness.h"
#include "game/Equipment.h"

using namespace lt::game;

namespace {

bool approx(float a, float b) { return std::fabs(a - b) < 1e-3f; }

bool hasHistory(const ItemInstance& it, const std::string& entry) {
    for (const auto& h : it.history)
        if (h == entry) return true;
    return false;
}

void putU64(std::vector<std::uint8_t>& b, std::size_t off, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) b[off + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
}

ItemCatalog makeCatalog() {
    ItemCatalog c;

    ItemDef blade;
    blade.id = "weapon.aetherfang";
    blade.kind = ItemKind::Weapon;
    blade.family = "greatblade";
    blade.slots = slotBit(Slot::MainHand);
    blade.weight = 7.2f;
    blade.maxDurability = 200;
    blade.sockets = {"core", "guard", "rune"};
    blade.aspectTags = {"motion", "memory"};
    c.add(blade);

    ItemDef helm;
    helm.id = "armor.frostbound_helm";
    helm.kind = ItemKind::Armour;
    helm.family = "heavy";
    helm.slots = slotBit(Slot::Head);
    helm.weight = 6.0f;
    helm.poise = 12.0f;
    helm.maxDurability = 150;
    helm.aspectTags = {"form"};
    c.add(helm);

    ItemDef plate;
    plate.id = "armor.frostbound_plate";
    plate.kind = ItemKind::Armour;
    plate.family = "heavy";
    plate.slots = slotBit(Slot::Torso);
    plate.weight = 18.0f;
    plate.poise = 30.0f;
    plate.maxDurability = 300;
    plate.aspectTags = {"form"};
    c.add(plate);

    ItemDef coreDef;
    coreDef.id = "attachment.aether_core";
    coreDef.kind = ItemKind::Attachment;
    coreDef.socketType = "core";
    coreDef.compatibleFamilies = {"greatblade", "longsword"};
    coreDef.weight = 0.8f;
    coreDef.aspectTags = {"motion"};
    c.add(coreDef);

    ItemDef lensDef;
    lensDef.id = "attachment.planar_lens";
    lensDef.kind = ItemKind::Attachment;
    lensDef.socketType = "rune";
    lensDef.compatibleFamilies = {"arcane_focus"};
    lensDef.weight = 0.4f;
    lensDef.aspectTags = {"light"};
    c.add(lensDef);

    ItemDef capDef;
    capDef.id = "attachment.back_capacitor";
    capDef.kind = ItemKind::Attachment;
    capDef.slots = kAttachmentSlotMask;
    capDef.socketType = "capacitor";
    capDef.weight = 2.5f;
    capDef.aspectTags = {"motion", "light"};
    c.add(capDef);

    ItemDef flask;
    flask.id = "consumable.vis_flask";
    flask.kind = ItemKind::Consumable;
    flask.weight = 0.3f;
    flask.maxDurability = 0;
    flask.maxStack = 5;
    c.add(flask);
    return c;
}

} // namespace

int main() {
    const ItemCatalog cat = makeCatalog();
    const ItemDef& bladeDef = *cat.find("weapon.aetherfang");
    const ItemDef& flaskDef = *cat.find("consumable.vis_flask");

    TEST_SECTION("names and catalog");
    CHECK(kSlotCount == 14);
    CHECK(std::string(toString(Slot::MainHand)) == "main_hand");
    CHECK(std::string(toString(Slot::Attachment4)) == "attachment_4");
    CHECK(std::string(toString(Rarity::Mythic)) == "mythic");
    CHECK(std::string(toString(InvError::SocketMismatch)) == "socket_mismatch");
    CHECK(cat.all().size() == 7);
    CHECK(cat.find("weapon.missing") == nullptr);
    {
        ItemCatalog copy = cat;
        ItemDef again;
        again.id = "weapon.aetherfang";
        CHECK(!copy.add(again));
        CHECK(!copy.add(ItemDef{}));
    }

    TEST_SECTION("deterministic generation");
    const ItemInstance a = generateItem(bladeDef, 1234, "drop:boss.frost_warden");
    const ItemInstance b = generateItem(bladeDef, 1234, "drop:boss.frost_warden");
    CHECK(a.maxDurability == b.maxDurability);
    CHECK(a.rarity == b.rarity);
    CHECK(a.history == b.history);
    CHECK(a.maxDurability >= 170 && a.maxDurability <= 200);
    CHECK(a.durability == a.maxDurability);
    CHECK(a.seed == 1234);
    CHECK(a.provenance == "drop:boss.frost_warden");
    CHECK(a.aspectTags.size() == 2);
    CHECK(a.sockets.size() == 3);
    CHECK(a.sockets[0].type == "core" && a.sockets[0].installed == 0);
    CHECK(hasHistory(a, "created:drop:boss.frost_warden"));
    CHECK(!a.broken());
    {
        bool differs = false;
        for (std::uint64_t s = 1; s < 64 && !differs; ++s) {
            const ItemInstance c = generateItem(bladeDef, 1234 + s, "drop:boss.frost_warden");
            differs = c.maxDurability != a.maxDurability || c.rarity != a.rarity;
        }
        CHECK(differs);
        int common = 0;
        int other = 0;
        for (std::uint64_t s = 0; s < 200; ++s) {
            if (generateItem(bladeDef, s, "roll").rarity == Rarity::Common) {
                ++common;
            } else {
                ++other;
            }
        }
        CHECK(common > 0 && other > 0);
        const ItemInstance flask = generateItem(flaskDef, 9, "craft");
        CHECK(flask.maxDurability == 0 && !flask.broken());
    }

    Inventory inv(cat, 40.0f);

    TEST_SECTION("empty loadout");
    {
        const EquipStats st = inv.stats();
        CHECK(approx(st.equippedWeight, 0.0f));
        CHECK(st.load == LoadClass::Light);
        CHECK(st.dodge == DodgeClass::Quick);
        CHECK(approx(st.staminaRecovery, 1.0f));
        CHECK(approx(st.noise, 1.0f));
    }

    TEST_SECTION("adding items");
    const std::uint64_t blade = inv.add(generateItem(bladeDef, 1, "start:class.warden"));
    const std::uint64_t blade2 = inv.add(generateItem(bladeDef, 2, "event.caravan_under_siege:vendor"));
    const std::uint64_t helm = inv.add(generateItem(*cat.find("armor.frostbound_helm"), 3, "vendor:aurelia"));
    const std::uint64_t plate =
        inv.add(generateItem(*cat.find("armor.frostbound_plate"), 4, "drop:enemy.frostbound_knight"));
    const std::uint64_t aetherCore =
        inv.add(generateItem(*cat.find("attachment.aether_core"), 5, "craft:device.infusion_altar"));
    const std::uint64_t aetherCore2 =
        inv.add(generateItem(*cat.find("attachment.aether_core"), 6, "craft:device.infusion_altar"));
    const std::uint64_t lens = inv.add(generateItem(*cat.find("attachment.planar_lens"), 7, "dungeon:rimeheart_hollow"));
    const std::uint64_t capacitor =
        inv.add(generateItem(*cat.find("attachment.back_capacitor"), 8, "research:device.flux_condenser"));
    CHECK(blade != 0 && blade2 != 0 && helm != 0 && plate != 0);
    CHECK(aetherCore != 0 && aetherCore2 != 0 && lens != 0 && capacitor != 0);
    CHECK(blade != blade2 && aetherCore != aetherCore2);
    CHECK(inv.items().size() == 8);
    CHECK(inv.inBag(blade) && !inv.isEquipped(blade));
    CHECK(inv.add(ItemInstance{}) == 0);
    CHECK(inv.items().size() == 8);

    TEST_SECTION("equip rules");
    CHECK(inv.equip(9999, Slot::Head) == InvError::UnknownItem);
    CHECK(inv.equip(blade, Slot::Head) == InvError::WrongSlot);
    CHECK(inv.equip(blade, Slot::Count) == InvError::WrongSlot);
    CHECK(inv.equip(blade, Slot::MainHand) == InvError::None);
    CHECK(inv.equipped(Slot::MainHand) == blade);
    CHECK(inv.isEquipped(blade) && !inv.inBag(blade));
    CHECK(inv.equip(blade, Slot::MainHand) == InvError::NotInBag);
    CHECK(inv.equip(blade2, Slot::MainHand) == InvError::None);  // swap: blade returns to the bag
    CHECK(inv.inBag(blade));
    CHECK(hasHistory(*inv.find(blade), "unequipped:main_hand"));
    CHECK(inv.equip(blade, Slot::MainHand) == InvError::None);
    CHECK(inv.inBag(blade2));
    CHECK(hasHistory(*inv.find(blade), "equipped:main_hand"));
    CHECK(inv.equip(helm, Slot::Head) == InvError::None);
    CHECK(inv.equip(capacitor, Slot::Attachment3) == InvError::None);
    CHECK(inv.equip(capacitor, Slot::Attachment1) == InvError::NotInBag);
    CHECK(inv.unequip(Slot::Relic) == InvError::Empty);
    CHECK(inv.unequip(Slot::Head) == InvError::None);
    CHECK(inv.inBag(helm));
    CHECK(inv.equip(helm, Slot::Head) == InvError::None);

    TEST_SECTION("attachments");
    CHECK(inv.install(blade, 0, 9999) == InvError::UnknownItem);
    CHECK(inv.install(blade, 0, helm) == InvError::NotAttachment);
    CHECK(inv.install(blade, 7, aetherCore) == InvError::BadSocket);
    CHECK(inv.install(blade, 1, aetherCore) == InvError::SocketMismatch);  // socket 1 is the guard
    CHECK(inv.install(blade, 2, lens) == InvError::IncompatibleHost);      // lens only fits arcane foci
    CHECK(inv.install(blade, 0, capacitor) == InvError::NotInBag);         // worn on the body
    CHECK(inv.install(blade, 0, aetherCore) == InvError::None);
    CHECK(inv.find(aetherCore)->socketedIn == blade);
    CHECK(inv.find(blade)->sockets[0].installed == aetherCore);
    CHECK(!inv.inBag(aetherCore));
    CHECK(inv.install(blade, 0, aetherCore2) == InvError::SocketOccupied);
    CHECK(inv.install(blade, 0, aetherCore) == InvError::NotInBag);
    CHECK(inv.install(aetherCore, 0, aetherCore2) == InvError::BadSocket);  // attachments never host
    CHECK(inv.equip(aetherCore, Slot::Attachment1) == InvError::WrongSlot);
    CHECK(inv.uninstall(blade, 1) == InvError::Empty);
    CHECK(inv.uninstall(blade, 9) == InvError::BadSocket);
    CHECK(inv.uninstall(blade, 0) == InvError::None);
    CHECK(inv.inBag(aetherCore));
    CHECK(inv.install(blade, 0, aetherCore) == InvError::None);
    CHECK(hasHistory(*inv.find(blade), "socketed:attachment.aether_core"));
    CHECK(hasHistory(*inv.find(aetherCore), "installed_in:weapon.aetherfang"));
    CHECK(inv.remove(aetherCore) == InvError::NotInBag);

    TEST_SECTION("weight and load");
    {
        const EquipStats st = inv.stats();  // blade 7.2 + core 0.8 + helm 6 + capacitor 2.5
        CHECK(approx(st.equippedWeight, 16.5f));
        CHECK(st.load == LoadClass::Medium);
        CHECK(st.dodge == DodgeClass::Standard);
        CHECK(approx(st.staminaRecovery, 0.85f));
        CHECK(approx(st.poise, 12.0f + 16.5f * 0.5f));
        CHECK(approx(st.noise, 1.0f + 16.5f / 40.0f));
    }
    CHECK(inv.equip(plate, Slot::Torso) == InvError::None);
    {
        const EquipStats st = inv.stats();
        CHECK(approx(st.equippedWeight, 34.5f));
        CHECK(st.load == LoadClass::Heavy);
        CHECK(st.dodge == DodgeClass::Heavy);
        CHECK(approx(st.staminaRecovery, 0.65f));
        CHECK(approx(st.poise, 42.0f + 34.5f * 0.5f));
    }
    inv.setCapacity(30.0f);
    CHECK(inv.stats().load == LoadClass::Overloaded);
    CHECK(inv.stats().dodge == DodgeClass::Stagger);
    CHECK(approx(inv.stats().staminaRecovery, 0.40f));
    inv.setCapacity(40.0f);
    CHECK(inv.stats().load == LoadClass::Heavy);

    TEST_SECTION("durability");
    {
        const std::uint32_t full = inv.find(blade)->durability;
        CHECK(inv.wear(Slot::MainHand, 10) == InvError::None);
        CHECK(inv.find(blade)->durability == full - 10);
        CHECK(inv.wear(Slot::Relic, 10) == InvError::Empty);
        const float poiseBefore = inv.stats().poise;
        CHECK(inv.wear(Slot::Head, 1000000) == InvError::None);
        CHECK(inv.find(helm)->broken());
        CHECK(hasHistory(*inv.find(helm), "broken"));
        CHECK(approx(inv.stats().poise, poiseBefore - 12.0f));
        CHECK(approx(inv.stats().equippedWeight, 34.5f));  // broken gear still weighs
        CHECK(inv.repair(helm) == InvError::None);
        CHECK(!inv.find(helm)->broken());
        CHECK(approx(inv.stats().poise, poiseBefore));
        CHECK(inv.repair(9999) == InvError::UnknownItem);
    }

    TEST_SECTION("stacks and quick belt");
    const std::uint64_t f1 = inv.add(generateItem(flaskDef, 20, "craft:field_kit"));
    for (int i = 0; i < 4; ++i) {
        CHECK(inv.add(generateItem(flaskDef, 21 + static_cast<std::uint64_t>(i), "craft:field_kit")) == f1);
    }
    CHECK(inv.find(f1) != nullptr && inv.find(f1)->count == 5);
    const std::uint64_t f2 = inv.add(generateItem(flaskDef, 30, "loot:caravan"));
    CHECK(f2 != 0 && f2 != f1);
    CHECK(inv.find(f2)->count == 1);
    CHECK(inv.assignBelt(kBeltSize, f1) == InvError::BeltIndex);
    CHECK(inv.assignBelt(-1, f1) == InvError::BeltIndex);
    CHECK(inv.assignBelt(0, blade2) == InvError::NotConsumable);
    CHECK(inv.assignBelt(0, 9999) == InvError::UnknownItem);
    CHECK(inv.assignBelt(0, f1) == InvError::None);
    CHECK(inv.belt(0) == f1);
    {
        std::string used;
        for (int i = 0; i < 5; ++i) CHECK(inv.useBelt(0, &used) == InvError::None);
        CHECK(used == "consumable.vis_flask");
    }
    CHECK(inv.find(f1) == nullptr);
    CHECK(inv.belt(0) == f2);  // refilled from the next stack
    CHECK(inv.useBelt(0) == InvError::None);
    CHECK(inv.find(f2) == nullptr);
    CHECK(inv.belt(0) == 0);
    CHECK(inv.useBelt(0) == InvError::Empty);
    CHECK(inv.useBelt(kBeltSize) == InvError::BeltIndex);
    const std::uint64_t f3 = inv.add(generateItem(flaskDef, 40, "vendor:aurelia"));
    CHECK(inv.assignBelt(3, f3) == InvError::None);

    TEST_SECTION("remove");
    CHECK(inv.remove(blade) == InvError::NotInBag);
    CHECK(inv.remove(lens) == InvError::None);
    CHECK(inv.find(lens) == nullptr);
    CHECK(inv.remove(lens) == InvError::UnknownItem);
    CHECK(inv.install(blade2, 0, aetherCore2) == InvError::None);
    CHECK(inv.remove(blade2) == InvError::None);
    CHECK(inv.find(blade2) == nullptr);
    CHECK(inv.find(aetherCore2) == nullptr);  // installed attachments leave with their host

    TEST_SECTION("deterministic replay");
    {
        auto script = [&](Inventory& v) {
            const std::uint64_t bl = v.add(generateItem(bladeDef, 77, "replay"));
            const std::uint64_t co = v.add(generateItem(*cat.find("attachment.aether_core"), 78, "replay"));
            v.equip(bl, Slot::MainHand);
            v.install(bl, 0, co);
            v.wear(Slot::MainHand, 3);
        };
        Inventory r1(cat);
        Inventory r2(cat);
        script(r1);
        script(r2);
        CHECK(r1.stateHash() == r2.stateHash());
        CHECK(r1.serialize() == r2.serialize());
        CHECK(r1.stateHash() != Inventory(cat).stateHash());
    }

    TEST_SECTION("save/reload");
    const std::vector<std::uint8_t> bytes = inv.serialize();
    Inventory loaded(cat);
    CHECK(loaded.deserialize(bytes));
    CHECK(loaded.serialize() == bytes);
    CHECK(loaded.stateHash() == inv.stateHash());
    CHECK(loaded.equipped(Slot::MainHand) == blade);
    CHECK(loaded.equipped(Slot::Torso) == plate);
    CHECK(loaded.equipped(Slot::Attachment3) == capacitor);
    CHECK(loaded.find(aetherCore) != nullptr && loaded.find(aetherCore)->socketedIn == blade);
    CHECK(loaded.belt(3) == f3);
    CHECK(approx(loaded.capacity(), 40.0f));
    CHECK(approx(loaded.stats().poise, inv.stats().poise));
    CHECK(loaded.stats().load == inv.stats().load);
    CHECK(loaded.find(blade)->seed == 1);
    CHECK(loaded.find(blade)->provenance == "start:class.warden");
    CHECK(loaded.find(blade)->history == inv.find(blade)->history);
    {
        const std::uint64_t fresh = loaded.add(generateItem(bladeDef, 99, "post-load"));
        bool collides = false;
        for (const auto& e : inv.items()) collides = collides || e.uid == fresh;
        CHECK(fresh != 0 && !collides);
    }

    TEST_SECTION("rejects bad data");
    {
        const std::vector<std::uint8_t> before = loaded.serialize();
        CHECK(!loaded.deserialize({}));
        CHECK(!loaded.deserialize(std::vector<std::uint8_t>{1, 2, 3}));
        std::vector<std::uint8_t> truncated = bytes;
        truncated.pop_back();
        CHECK(!loaded.deserialize(truncated));
        std::vector<std::uint8_t> trailing = bytes;
        trailing.push_back(0);
        CHECK(!loaded.deserialize(trailing));
        ItemCatalog emptyCatalog;
        Inventory strict(emptyCatalog);
        CHECK(!strict.deserialize(bytes));
        const std::size_t beltBytes = static_cast<std::size_t>(kBeltSize) * 8;
        const std::size_t slotBytes = static_cast<std::size_t>(kSlotCount) * 8;
        std::vector<std::uint8_t> badBelt = bytes;
        putU64(badBelt, badBelt.size() - beltBytes + 3 * 8, blade);  // belt slot 3 -> a weapon
        CHECK(!loaded.deserialize(badBelt));
        std::vector<std::uint8_t> badSlot = bytes;
        putU64(badSlot, badSlot.size() - beltBytes - slotBytes, plate);  // head slot -> torso armour
        CHECK(!loaded.deserialize(badSlot));
        std::vector<std::uint8_t> dupSlot = bytes;
        putU64(dupSlot, dupSlot.size() - beltBytes - slotBytes + static_cast<std::size_t>(Slot::Attachment1) * 8,
               capacitor);  // the same capacitor worn in two slots
        CHECK(!loaded.deserialize(dupSlot));
        CHECK(loaded.serialize() == before);
    }

    return ::lt::test::finish("equipment");
}

#pragma once
// ThaumtMourge equipment and inventory (design doc Sections 6, 7, 12, 14).
// Equipment slots: head, torso, hands, legs, cloak, main hand, off hand, ranged, focus,
// four attachment slots, relic, and a quick-use belt. Equipped weight drives poise,
// stamina recovery, noise and dodge animation class. Every generated item stores its
// seed, provenance, aspect tags, durability, sockets and history; rarity is only a
// secondary label. Item ids are stable keys - display text lives in localisation data.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tcore/Random.h"
#include "tcore/Serialize.h"

namespace lt::game {

enum class Slot : std::uint8_t {
    Head = 0, Torso, Hands, Legs, Cloak, MainHand, OffHand, Ranged, Focus,
    Attachment1, Attachment2, Attachment3, Attachment4, Relic, Count
};
inline constexpr int kSlotCount = static_cast<int>(Slot::Count);
inline constexpr int kBeltSize = 8;             // quick-use belt (number keys on desktop)
inline constexpr std::size_t kMaxHistory = 64;  // newest history entries kept per item

inline const char* toString(Slot s) noexcept {
    switch (s) {
        case Slot::Head: return "head";
        case Slot::Torso: return "torso";
        case Slot::Hands: return "hands";
        case Slot::Legs: return "legs";
        case Slot::Cloak: return "cloak";
        case Slot::MainHand: return "main_hand";
        case Slot::OffHand: return "off_hand";
        case Slot::Ranged: return "ranged";
        case Slot::Focus: return "focus";
        case Slot::Attachment1: return "attachment_1";
        case Slot::Attachment2: return "attachment_2";
        case Slot::Attachment3: return "attachment_3";
        case Slot::Attachment4: return "attachment_4";
        case Slot::Relic: return "relic";
        case Slot::Count: break;
    }
    return "unknown";
}

constexpr std::uint32_t slotBit(Slot s) noexcept { return 1u << static_cast<unsigned>(s); }
inline constexpr std::uint32_t kAttachmentSlotMask = slotBit(Slot::Attachment1) | slotBit(Slot::Attachment2) |
                                                     slotBit(Slot::Attachment3) | slotBit(Slot::Attachment4);

enum class ItemKind : std::uint8_t { Armour = 0, Weapon, Focus, Attachment, Relic, Consumable, Material };
enum class Rarity : std::uint8_t { Common = 0, Uncommon, Rare, Mythic };

inline const char* toString(Rarity r) noexcept {
    switch (r) {
        case Rarity::Common: return "common";
        case Rarity::Uncommon: return "uncommon";
        case Rarity::Rare: return "rare";
        case Rarity::Mythic: return "mythic";
    }
    return "unknown";
}

namespace detail {

inline std::uint64_t fnv1a(const void* data, std::size_t n) noexcept {
    const auto* p = static_cast<const unsigned char*>(data);
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

inline std::uint64_t idHash(std::string_view s) noexcept { return fnv1a(s.data(), s.size()); }

template <class T>
bool contains(const std::vector<T>& v, const T& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

} // namespace detail

// Static item definition (Section 21 item record plus equipment metadata).
struct ItemDef {
    std::string id;                               // stable key, e.g. weapon.aetherfang
    ItemKind kind = ItemKind::Material;
    std::string family;                           // weapon family or armour class
    std::uint32_t slots = 0;                      // bitmask of Slot values it may occupy
    float weight = 0.0f;
    float poise = 0.0f;                           // armour poise contribution
    std::uint32_t maxDurability = 100;            // 0 = indestructible
    std::vector<std::string> sockets;             // host sockets, e.g. core, guard, rune
    std::string socketType;                       // attachments: socket they fit
    std::vector<std::string> compatibleFamilies;  // attachments: allowed host families (empty = any)
    std::vector<std::string> aspectTags;
    std::uint32_t maxStack = 1;
};

class ItemCatalog {
public:
    bool add(ItemDef def) {
        if (def.id.empty() || find(def.id) != nullptr) return false;
        defs_.push_back(std::move(def));
        return true;
    }
    const ItemDef* find(std::string_view id) const noexcept {
        for (const auto& d : defs_)
            if (d.id == id) return &d;
        return nullptr;
    }
    const std::vector<ItemDef>& all() const noexcept { return defs_; }

private:
    std::vector<ItemDef> defs_;
};

struct Socket {
    std::string type;
    std::uint64_t installed = 0;  // uid of the installed attachment, 0 = empty
};

// Runtime item: everything needed to explain where it came from and what happened to it.
struct ItemInstance {
    std::uint64_t uid = 0;
    std::string defId;
    std::uint64_t seed = 0;
    std::string provenance;
    std::vector<std::string> aspectTags;
    std::uint32_t durability = 0;
    std::uint32_t maxDurability = 0;
    std::vector<Socket> sockets;
    std::vector<std::string> history;
    Rarity rarity = Rarity::Common;
    std::uint32_t count = 1;
    std::uint64_t socketedIn = 0;  // host uid while installed as an attachment

    bool broken() const noexcept { return maxDurability > 0 && durability == 0; }
};

inline void pushHistory(ItemInstance& it, std::string entry) {
    if (it.history.size() >= kMaxHistory) it.history.erase(it.history.begin());
    it.history.push_back(std::move(entry));
}

// Deterministic generation: the same definition, seed and provenance give the same item.
inline ItemInstance generateItem(const ItemDef& def, std::uint64_t seed, std::string provenance) {
    core::Rng rng(core::hashCombine(seed, detail::idHash(def.id)));
    ItemInstance it;
    it.defId = def.id;
    it.seed = seed;
    it.provenance = std::move(provenance);
    it.aspectTags = def.aspectTags;
    if (def.maxDurability > 0) {
        // Seeded quality: 85-100% of the definition's durability.
        const std::uint64_t maxD = def.maxDurability;
        const std::uint64_t lo = maxD - maxD * 15u / 100u;
        it.maxDurability = static_cast<std::uint32_t>(lo + rng.below(maxD - lo + 1));
        it.durability = it.maxDurability;
    }
    const std::uint64_t roll = rng.below(100);
    it.rarity = roll < 60 ? Rarity::Common : roll < 85 ? Rarity::Uncommon : roll < 97 ? Rarity::Rare : Rarity::Mythic;
    it.sockets.reserve(def.sockets.size());
    for (const auto& s : def.sockets) it.sockets.push_back(Socket{s, 0});
    it.history.push_back("created:" + it.provenance);
    return it;
}

enum class InvError : std::uint8_t {
    None = 0, UnknownItem, UnknownDef, WrongSlot, NotInBag, NotAttachment, BadSocket,
    SocketOccupied, SocketMismatch, IncompatibleHost, NotConsumable, BeltIndex, Empty
};

inline const char* toString(InvError e) noexcept {
    switch (e) {
        case InvError::None: return "none";
        case InvError::UnknownItem: return "unknown_item";
        case InvError::UnknownDef: return "unknown_def";
        case InvError::WrongSlot: return "wrong_slot";
        case InvError::NotInBag: return "not_in_bag";
        case InvError::NotAttachment: return "not_attachment";
        case InvError::BadSocket: return "bad_socket";
        case InvError::SocketOccupied: return "socket_occupied";
        case InvError::SocketMismatch: return "socket_mismatch";
        case InvError::IncompatibleHost: return "incompatible_host";
        case InvError::NotConsumable: return "not_consumable";
        case InvError::BeltIndex: return "belt_index";
        case InvError::Empty: return "empty";
    }
    return "unknown";
}

enum class LoadClass : std::uint8_t { Light = 0, Medium, Heavy, Overloaded };
// Dodge animation set chosen from equip load (Section 7: heavier armour, slower recovery).
enum class DodgeClass : std::uint8_t { Quick = 0, Standard, Heavy, Stagger };

inline LoadClass loadClassFor(float ratio) noexcept {
    if (ratio <= 0.30f) return LoadClass::Light;
    if (ratio <= 0.70f) return LoadClass::Medium;
    if (ratio <= 1.00f) return LoadClass::Heavy;
    return LoadClass::Overloaded;
}

struct EquipStats {
    float equippedWeight = 0.0f;
    float capacity = 0.0f;
    float loadRatio = 0.0f;
    LoadClass load = LoadClass::Light;
    DodgeClass dodge = DodgeClass::Quick;
    float poise = 0.0f;            // armour poise + mass
    float staminaRecovery = 1.0f;  // multiplier on stamina regeneration
    float noise = 1.0f;            // multiplier on enemy perception range
};

// Bag + equipment + quick belt for one character (Section 14 character save layer).
class Inventory {
public:
    static constexpr std::string_view kMagic = "inventory_v1";

    explicit Inventory(const ItemCatalog& catalog, float capacity = 60.0f) noexcept
        : catalog_(&catalog), capacity_(capacity < 0.0f ? 0.0f : capacity) {}

    // Equipment capacity grows with the Rank progression track (Section 6).
    void setCapacity(float capacity) noexcept { capacity_ = capacity < 0.0f ? 0.0f : capacity; }
    float capacity() const noexcept { return capacity_; }

    const ItemInstance* find(std::uint64_t uid) const noexcept {
        if (uid == 0) return nullptr;
        for (const auto& e : items_)
            if (e.uid == uid) return &e;
        return nullptr;
    }
    const std::vector<ItemInstance>& items() const noexcept { return items_; }
    std::uint64_t equipped(Slot s) const noexcept { return s < Slot::Count ? equipped_[idx(s)] : 0; }
    std::uint64_t belt(int i) const noexcept {
        return (i >= 0 && i < kBeltSize) ? belt_[static_cast<std::size_t>(i)] : 0;
    }

    bool isEquipped(std::uint64_t uid) const noexcept {
        if (uid == 0) return false;
        for (std::uint64_t u : equipped_)
            if (u == uid) return true;
        return false;
    }

    // In the bag: owned, not worn, and not installed in another item's socket.
    bool inBag(std::uint64_t uid) const noexcept {
        const ItemInstance* it = find(uid);
        return it != nullptr && it->socketedIn == 0 && !isEquipped(uid);
    }

    // Adds an item to the bag; returns its uid, or 0 for an unknown definition.
    // Stackable items top up existing bag stacks first and the uid of the last
    // stack that received units is returned.
    std::uint64_t add(ItemInstance item) {
        const ItemDef* def = catalog_->find(item.defId);
        if (def == nullptr) return 0;
        const std::uint32_t maxStack = def->maxStack == 0 ? 1u : def->maxStack;
        std::uint32_t remaining = item.count == 0 ? 1u : item.count;
        std::uint64_t last = 0;
        if (maxStack > 1) {
            for (auto& e : items_) {
                if (remaining == 0) break;
                if (e.defId != item.defId || e.count >= maxStack || !inBag(e.uid)) continue;
                const std::uint32_t moved = std::min(remaining, maxStack - e.count);
                e.count += moved;
                remaining -= moved;
                last = e.uid;
            }
        }
        item.socketedIn = 0;
        for (auto& s : item.sockets) s.installed = 0;
        while (remaining > 0) {
            ItemInstance fresh = item;
            fresh.uid = nextUid_++;
            fresh.count = std::min(remaining, maxStack);
            remaining -= fresh.count;
            last = fresh.uid;
            items_.push_back(std::move(fresh));
        }
        return last;
    }

    // Removes a bag item (and anything installed in its sockets) from the inventory.
    InvError remove(std::uint64_t uid) {
        const ItemInstance* it = find(uid);
        if (it == nullptr) return InvError::UnknownItem;
        if (!inBag(uid)) return InvError::NotInBag;
        std::vector<std::uint64_t> doomed{uid};
        for (const auto& s : it->sockets)
            if (s.installed != 0) doomed.push_back(s.installed);
        for (std::uint64_t d : doomed) clearBeltRefs(d);
        std::erase_if(items_, [&doomed](const ItemInstance& e) { return detail::contains(doomed, e.uid); });
        return InvError::None;
    }

    // Equips a bag item; the previous occupant of the slot returns to the bag.
    InvError equip(std::uint64_t uid, Slot slot) {
        if (slot >= Slot::Count) return InvError::WrongSlot;
        ItemInstance* it = findMut(uid);
        if (it == nullptr) return InvError::UnknownItem;
        const ItemDef* def = catalog_->find(it->defId);
        if (def == nullptr) return InvError::UnknownDef;
        if ((def->slots & slotBit(slot)) == 0) return InvError::WrongSlot;
        if (!inBag(uid)) return InvError::NotInBag;
        std::uint64_t& occupant = equipped_[idx(slot)];
        if (ItemInstance* prev = findMut(occupant)) {
            pushHistory(*prev, std::string("unequipped:") + toString(slot));
        }
        clearBeltRefs(uid);
        occupant = uid;
        pushHistory(*it, std::string("equipped:") + toString(slot));
        return InvError::None;
    }

    InvError unequip(Slot slot) {
        if (slot >= Slot::Count) return InvError::WrongSlot;
        std::uint64_t& occupant = equipped_[idx(slot)];
        if (occupant == 0) return InvError::Empty;
        if (ItemInstance* it = findMut(occupant)) {
            pushHistory(*it, std::string("unequipped:") + toString(slot));
        }
        occupant = 0;
        return InvError::None;
    }

    // Installs a bag attachment into a host item's socket (Section 7 compatibility tags).
    InvError install(std::uint64_t hostUid, std::size_t socket, std::uint64_t attachmentUid) {
        ItemInstance* host = findMut(hostUid);
        ItemInstance* att = findMut(attachmentUid);
        if (host == nullptr || att == nullptr || hostUid == attachmentUid) return InvError::UnknownItem;
        const ItemDef* hostDef = catalog_->find(host->defId);
        const ItemDef* attDef = catalog_->find(att->defId);
        if (hostDef == nullptr || attDef == nullptr) return InvError::UnknownDef;
        if (attDef->kind != ItemKind::Attachment) return InvError::NotAttachment;
        if (!inBag(attachmentUid)) return InvError::NotInBag;
        if (hostDef->kind == ItemKind::Attachment || host->socketedIn != 0) return InvError::BadSocket;
        if (socket >= host->sockets.size()) return InvError::BadSocket;
        Socket& s = host->sockets[socket];
        if (s.installed != 0) return InvError::SocketOccupied;
        if (attDef->socketType != s.type) return InvError::SocketMismatch;
        if (!attDef->compatibleFamilies.empty() && !detail::contains(attDef->compatibleFamilies, hostDef->family))
            return InvError::IncompatibleHost;
        clearBeltRefs(attachmentUid);
        s.installed = attachmentUid;
        att->socketedIn = hostUid;
        pushHistory(*host, "socketed:" + att->defId);
        pushHistory(*att, "installed_in:" + host->defId);
        return InvError::None;
    }

    InvError uninstall(std::uint64_t hostUid, std::size_t socket) {
        ItemInstance* host = findMut(hostUid);
        if (host == nullptr) return InvError::UnknownItem;
        if (socket >= host->sockets.size()) return InvError::BadSocket;
        Socket& s = host->sockets[socket];
        if (s.installed == 0) return InvError::Empty;
        if (ItemInstance* att = findMut(s.installed)) {
            att->socketedIn = 0;
            pushHistory(*att, "removed_from:" + host->defId);
        }
        s.installed = 0;
        pushHistory(*host, "unsocketed:" + s.type);
        return InvError::None;
    }

    // Assigns a bag consumable stack to a quick-use belt slot (uid 0 clears the slot).
    InvError assignBelt(int index, std::uint64_t uid) {
        if (index < 0 || index >= kBeltSize) return InvError::BeltIndex;
        const std::size_t slot = static_cast<std::size_t>(index);
        if (uid == 0) {
            belt_[slot] = 0;
            return InvError::None;
        }
        const ItemInstance* it = find(uid);
        if (it == nullptr) return InvError::UnknownItem;
        const ItemDef* def = catalog_->find(it->defId);
        if (def == nullptr) return InvError::UnknownDef;
        if (def->kind != ItemKind::Consumable) return InvError::NotConsumable;
        if (!inBag(uid)) return InvError::NotInBag;
        belt_[slot] = uid;
        return InvError::None;
    }

    // Uses one unit from a belt slot; an emptied stack is refilled from the next bag stack.
    InvError useBelt(int index, std::string* usedDef = nullptr) {
        if (index < 0 || index >= kBeltSize) return InvError::BeltIndex;
        const std::size_t slot = static_cast<std::size_t>(index);
        const std::uint64_t uid = belt_[slot];
        ItemInstance* it = findMut(uid);
        if (it == nullptr) {
            belt_[slot] = 0;
            return InvError::Empty;
        }
        const std::string defId = it->defId;
        if (usedDef != nullptr) *usedDef = defId;
        if (it->count > 1) {
            --it->count;
            return InvError::None;
        }
        clearBeltRefs(uid);
        std::erase_if(items_, [uid](const ItemInstance& e) { return e.uid == uid; });
        for (const auto& e : items_) {
            if (e.defId == defId && inBag(e.uid)) {
                belt_[slot] = e.uid;
                break;
            }
        }
        return InvError::None;
    }

    // Durability loss on the item worn in a slot; reaching zero marks it broken.
    InvError wear(Slot slot, std::uint32_t amount) {
        if (slot >= Slot::Count) return InvError::WrongSlot;
        ItemInstance* it = findMut(equipped_[idx(slot)]);
        if (it == nullptr) return InvError::Empty;
        if (it->maxDurability == 0 || it->durability == 0) return InvError::None;
        it->durability = amount >= it->durability ? 0u : it->durability - amount;
        if (it->durability == 0) pushHistory(*it, "broken");
        return InvError::None;
    }

    InvError repair(std::uint64_t uid) {
        ItemInstance* it = findMut(uid);
        if (it == nullptr) return InvError::UnknownItem;
        it->durability = it->maxDurability;
        pushHistory(*it, "repaired");
        return InvError::None;
    }

    // Section 12: weight influences poise, stamina recovery, noise and dodge animation.
    EquipStats stats() const {
        static constexpr float kRecovery[] = {1.0f, 0.85f, 0.65f, 0.40f};
        EquipStats st;
        st.capacity = capacity_;
        float armourPoise = 0.0f;
        for (std::uint64_t u : equipped_) {
            const ItemInstance* it = find(u);
            if (it == nullptr) continue;
            const ItemDef* def = catalog_->find(it->defId);
            if (def == nullptr) continue;
            st.equippedWeight += def->weight;
            if (!it->broken()) armourPoise += def->poise;  // broken gear still weighs
            for (const auto& s : it->sockets) {
                const ItemInstance* a = find(s.installed);
                const ItemDef* ad = a != nullptr ? catalog_->find(a->defId) : nullptr;
                if (ad != nullptr) st.equippedWeight += ad->weight;
            }
        }
        if (capacity_ > 0.0f) {
            st.loadRatio = st.equippedWeight / capacity_;
        } else {
            st.loadRatio = st.equippedWeight > 0.0f ? 2.0f : 0.0f;
        }
        st.load = loadClassFor(st.loadRatio);
        st.dodge = static_cast<DodgeClass>(st.load);
        st.staminaRecovery = kRecovery[static_cast<std::size_t>(st.load)];
        st.poise = armourPoise + st.equippedWeight * 0.5f;
        st.noise = 1.0f + st.equippedWeight / 40.0f;
        return st;
    }

    std::vector<std::uint8_t> serialize() const {
        core::ByteWriter w;
        w.str(kMagic);
        w.f32(capacity_);
        w.u64(nextUid_);
        w.u32(static_cast<std::uint32_t>(items_.size()));
        for (const auto& it : items_) {
            w.u64(it.uid);
            w.str(it.defId);
            w.u64(it.seed);
            w.str(it.provenance);
            w.strList(it.aspectTags);
            w.u32(it.durability);
            w.u32(it.maxDurability);
            w.u32(static_cast<std::uint32_t>(it.sockets.size()));
            for (const auto& s : it.sockets) {
                w.str(s.type);
                w.u64(s.installed);
            }
            w.strList(it.history);
            w.u8(static_cast<std::uint8_t>(it.rarity));
            w.u32(it.count);
            w.u64(it.socketedIn);
        }
        for (std::uint64_t u : equipped_) w.u64(u);
        for (std::uint64_t u : belt_) w.u64(u);
        return w.take();
    }

    // Restores a serialize() image. On malformed or inconsistent data the inventory
    // is left untouched and false is returned.
    bool deserialize(const std::vector<std::uint8_t>& bytes) {
        core::ByteReader r(bytes);
        const std::string magic = r.str(kMagic.size() + 8);
        if (!r.ok() || magic != std::string(kMagic)) return false;
        const float capacity = r.f32();
        const std::uint64_t nextUid = r.u64();
        const std::uint32_t n = r.count(8);
        std::vector<ItemInstance> items;
        items.reserve(n);
        for (std::uint32_t i = 0; i < n && r.ok(); ++i) {
            ItemInstance it;
            it.uid = r.u64();
            it.defId = r.str(256);
            it.seed = r.u64();
            it.provenance = r.str(1024);
            it.aspectTags = r.strList(64);
            it.durability = r.u32();
            it.maxDurability = r.u32();
            const std::uint32_t sockets = r.count(12, 64);
            for (std::uint32_t k = 0; k < sockets && r.ok(); ++k) {
                Socket s;
                s.type = r.str(64);
                s.installed = r.u64();
                it.sockets.push_back(std::move(s));
            }
            it.history = r.strList(kMaxHistory);
            const std::uint8_t rarity = r.u8();
            if (rarity > static_cast<std::uint8_t>(Rarity::Mythic)) r.fail();
            it.rarity = static_cast<Rarity>(rarity);
            it.count = r.u32();
            it.socketedIn = r.u64();
            items.push_back(std::move(it));
        }
        std::array<std::uint64_t, kSlotCount> equipped{};
        for (auto& u : equipped) u = r.u64();
        std::array<std::uint64_t, kBeltSize> belt{};
        for (auto& u : belt) u = r.u64();
        if (!r.finished() || !(capacity >= 0.0f) || nextUid == 0) return false;
        if (!validate(items, equipped, belt, nextUid)) return false;
        capacity_ = capacity;
        nextUid_ = nextUid;
        items_ = std::move(items);
        equipped_ = equipped;
        belt_ = belt;
        return true;
    }

    // Section 19 simulation gate: identical operation sequences give identical hashes.
    std::uint64_t stateHash() const {
        const std::vector<std::uint8_t> b = serialize();
        return detail::fnv1a(b.data(), b.size());
    }

private:
    static constexpr std::size_t idx(Slot s) noexcept { return static_cast<std::size_t>(s); }

    ItemInstance* findMut(std::uint64_t uid) noexcept {
        return const_cast<ItemInstance*>(std::as_const(*this).find(uid));
    }

    void clearBeltRefs(std::uint64_t uid) noexcept {
        for (auto& b : belt_)
            if (b == uid) b = 0;
    }

    bool validate(const std::vector<ItemInstance>& items, const std::array<std::uint64_t, kSlotCount>& equipped,
                  const std::array<std::uint64_t, kBeltSize>& belt, std::uint64_t nextUid) const {
        auto lookup = [&items](std::uint64_t uid) -> const ItemInstance* {
            if (uid == 0) return nullptr;
            for (const auto& e : items)
                if (e.uid == uid) return &e;
            return nullptr;
        };
        for (std::size_t i = 0; i < items.size(); ++i) {
            const ItemInstance& it = items[i];
            if (it.uid == 0 || it.uid >= nextUid) return false;
            for (std::size_t j = 0; j < i; ++j)
                if (items[j].uid == it.uid) return false;
            const ItemDef* def = catalog_->find(it.defId);
            if (def == nullptr) return false;
            const std::uint32_t maxStack = def->maxStack == 0 ? 1u : def->maxStack;
            if (it.count == 0 || it.count > maxStack || it.durability > it.maxDurability) return false;
        }
        for (const auto& it : items) {
            for (const auto& s : it.sockets) {
                if (s.installed == 0) continue;
                const ItemInstance* a = lookup(s.installed);
                if (a == nullptr || a->socketedIn != it.uid) return false;
            }
            if (it.socketedIn != 0) {
                const ItemInstance* h = lookup(it.socketedIn);
                if (h == nullptr) return false;
                int refs = 0;
                for (const auto& s : h->sockets)
                    if (s.installed == it.uid) ++refs;
                if (refs != 1) return false;
            }
        }
        auto worn = [&equipped](std::uint64_t uid) {
            for (std::uint64_t u : equipped)
                if (u == uid) return true;
            return false;
        };
        for (std::size_t i = 0; i < equipped.size(); ++i) {
            const std::uint64_t u = equipped[i];
            if (u == 0) continue;
            const ItemInstance* it = lookup(u);
            if (it == nullptr || it->socketedIn != 0) return false;
            const ItemDef* def = catalog_->find(it->defId);
            if (def == nullptr || (def->slots & slotBit(static_cast<Slot>(i))) == 0) return false;
            for (std::size_t j = 0; j < i; ++j)
                if (equipped[j] == u) return false;
        }
        for (std::uint64_t u : belt) {
            if (u == 0) continue;
            const ItemInstance* it = lookup(u);
            if (it == nullptr || it->socketedIn != 0 || worn(u)) return false;
            const ItemDef* def = catalog_->find(it->defId);
            if (def == nullptr || def->kind != ItemKind::Consumable) return false;
        }
        return true;
    }

    const ItemCatalog* catalog_;
    float capacity_;
    std::uint64_t nextUid_ = 1;
    std::vector<ItemInstance> items_;
    std::array<std::uint64_t, kSlotCount> equipped_{};
    std::array<std::uint64_t, kBeltSize> belt_{};
};

} // namespace lt::game

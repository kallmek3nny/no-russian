#pragma once
// ThaumtMourge character creation (design doc Sections 2.5, 4.3, 6, 26): name, body silhouette,
// voice set, origin memory, face palette, scars, cloak, starting city connection and class.
// Class is a starting toolkit (Progression.h). The origin memory is the forgotten past revealed by
// optional memory shrines; once revealed it changes how some factions judge the player.
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "game/Progression.h"
#include "tcore/Random.h"

namespace lt::game {

enum class BodySilhouette : std::uint8_t { Slight = 0, Lean, Broad, Heavy };
enum class VoiceSet : std::uint8_t { Low = 0, Mid, High, Rasp };
enum class CloakStyle : std::uint8_t { Mantle = 0, Hooded, Tattered, Shawl };
enum class OriginMemory : std::uint8_t {
    VaultScholar = 0, OrderDeserter, CaravanOrphan, ForgeApprentice, GroveFoundling, CouncilWard
};
enum class StartingCity : std::uint8_t { Aurelia = 0, Mournhollow, VeyrHarbor, KharDuun, VerdantCourt };

// Stable save/localisation ids, in enum order. City ids match world::CityDef::id (Section 4.3).
inline constexpr std::array<std::string_view, 4> kSilhouetteNames{"slight", "lean", "broad", "heavy"};
inline constexpr std::array<std::string_view, 4> kVoiceNames{"low", "mid", "high", "rasp"};
inline constexpr std::array<std::string_view, 4> kCloakNames{"mantle", "hooded", "tattered", "shawl"};
inline constexpr std::array<std::string_view, 6> kOriginNames{"vault_scholar", "order_deserter", "caravan_orphan",
                                                              "forge_apprentice", "grove_foundling", "council_ward"};
inline constexpr std::array<std::string_view, 5> kStartingCityIds{"city.aurelia", "city.mournhollow", "city.veyr_harbor",
                                                                  "city.khar_duun", "city.verdant_court"};
inline constexpr std::array<std::string_view, 8> kScarSites{"brow", "cheek", "jaw", "lip", "ear", "neck", "eye", "burn"};

inline constexpr std::uint8_t kFacePaletteCount = 8;  // limited colour families (Section 16)
inline constexpr std::size_t kMaxNameCodepoints = 24;
inline constexpr int kMaxScars = 3;

struct CharacterSheet {
    std::string name;  // UTF-8; Cyrillic and other scripts are allowed (Section 26)
    BodySilhouette silhouette = BodySilhouette::Lean;
    VoiceSet voice = VoiceSet::Mid;
    OriginMemory origin = OriginMemory::VaultScholar;
    std::uint8_t facePalette = 0;
    std::uint8_t scars = 0;  // bit i set = scar at kScarSites[i]
    CloakStyle cloak = CloakStyle::Mantle;
    StartingCity city = StartingCity::Aurelia;
    StartClass cls = StartClass::Warden;
    bool originRevealed = false;  // set by the first memory shrine
    friend bool operator==(const CharacterSheet&, const CharacterSheet&) = default;
};

enum class CreationError : std::uint8_t {
    None = 0, EmptyName, NameTooLong, BadName, BadAppearance, TooManyScars, BadChoice
};

// UTF-8 code point count, or -1 for malformed sequences and control characters.
inline long nameCodepoints(std::string_view s) noexcept {
    long n = 0;
    for (std::size_t i = 0; i < s.size(); ++n) {
        const auto c = static_cast<unsigned char>(s[i]);
        const std::size_t len = c < 0x80 ? 1 : (c >= 0xC2 && c < 0xE0) ? 2 : (c >= 0xE0 && c < 0xF0) ? 3
                              : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
        if (len == 0 || i + len > s.size() || (len == 1 && (c < 0x20 || c == 0x7F))) return -1;
        for (std::size_t k = 1; k < len; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return -1;
        i += len;
    }
    return n;
}

inline int scarCount(std::uint8_t mask) noexcept {
    int n = 0;
    for (unsigned m = mask; m != 0; m &= m - 1) ++n;
    return n;
}

// Creation gate: nothing is committed to a save until the sheet validates.
inline CreationError validate(const CharacterSheet& c) noexcept {
    const long n = nameCodepoints(c.name);
    if (n < 0) return CreationError::BadName;
    if (n == 0) return CreationError::EmptyName;
    if (static_cast<std::size_t>(n) > kMaxNameCodepoints) return CreationError::NameTooLong;
    if (c.name.front() == ' ' || c.name.back() == ' ') return CreationError::BadName;
    if (c.silhouette > BodySilhouette::Heavy || c.voice > VoiceSet::Rasp || c.cloak > CloakStyle::Shawl ||
        c.facePalette >= kFacePaletteCount)
        return CreationError::BadAppearance;
    if (scarCount(c.scars) > kMaxScars) return CreationError::TooManyScars;
    if (c.origin > OriginMemory::CouncilWard || c.city > StartingCity::VerdantCourt ||
        static_cast<std::size_t>(c.cls) >= kStartClassCount)
        return CreationError::BadChoice;
    return CreationError::None;
}

inline std::string_view cityId(StartingCity c) noexcept {
    return c <= StartingCity::VerdantCourt ? kStartingCityIds[static_cast<std::size_t>(c)] : "unknown";
}

// Memory shrine visit (Section 2.5): the first shrine reveals the forgotten past. Returns true
// only on the visit that reveals it; later shrines are lore, not a second judgement.
inline bool revealOrigin(CharacterSheet& c) noexcept {
    if (c.originRevealed) return false;
    c.originRevealed = true;
    return true;
}

// How factions judge a revealed origin, as a standing shift in Reputation's [-1000, 1000] range.
// Every origin wins one faction's trust and costs another's; unlisted pairs are neutral.
struct OriginJudgement {
    OriginMemory origin;
    Faction faction;
    int delta;
};
inline constexpr std::array<OriginJudgement, 12> kOriginJudgements{{
    {OriginMemory::VaultScholar, Faction::OrderOfThaumiel, 100},
    {OriginMemory::VaultScholar, Faction::AshTribunal, -100},
    {OriginMemory::OrderDeserter, Faction::VeyrSmugglers, 100},
    {OriginMemory::OrderDeserter, Faction::OrderOfThaumiel, -150},
    {OriginMemory::CaravanOrphan, Faction::FreeCaravans, 150},
    {OriginMemory::CaravanOrphan, Faction::VeyrSmugglers, -50},
    {OriginMemory::ForgeApprentice, Faction::KharDuunMiners, 150},
    {OriginMemory::ForgeApprentice, Faction::VerdantCourt, -100},
    {OriginMemory::GroveFoundling, Faction::VerdantCourt, 150},
    {OriginMemory::GroveFoundling, Faction::KharDuunMiners, -100},
    {OriginMemory::CouncilWard, Faction::TautmaurgRemnants, 150},
    {OriginMemory::CouncilWard, Faction::AshTribunal, -150},
}};

inline constexpr int originJudgement(OriginMemory o, Faction f) noexcept {
    int delta = 0;
    for (const auto& j : kOriginJudgements)
        if (j.origin == o && j.faction == f) delta += j.delta;
    return delta;
}

// Standing shift to apply once, when revealOrigin() first returns true. Hidden origins are neutral.
inline int factionJudgement(const CharacterSheet& c, Faction f) noexcept {
    return c.originRevealed ? originJudgement(c.origin, f) : 0;
}

} // namespace lt::game

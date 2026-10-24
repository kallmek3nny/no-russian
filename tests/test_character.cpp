// Sections 2.5/4.3/6/26 character creation: sheet validation, UTF-8 (Cyrillic) names, scars,
// starting city ids, and the memory-shrine origin reveal that changes faction judgement.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "TestHarness.h"
#include "game/Character.h"

using namespace lt::game;

static_assert(originJudgement(OriginMemory::ForgeApprentice, Faction::KharDuunMiners) == 150);
static_assert(originJudgement(OriginMemory::ForgeApprentice, Faction::MourgeCults) == 0);

namespace {
CharacterSheet named(std::string name) {
    CharacterSheet c;
    c.name = std::move(name);
    return c;
}
std::string repeat(std::string_view unit, std::size_t n) {
    std::string s;
    for (std::size_t i = 0; i < n; ++i) s += unit;
    return s;
}
}  // namespace

int main() {
    TEST_SECTION("defaults");
    CHECK(validate(CharacterSheet{}) == CreationError::EmptyName);
    CHECK(validate(named("Kestrel")) == CreationError::None);
    CHECK(!named("Kestrel").originRevealed);

    // Escaped literals live outside the CHECK macros so stringizing never sees a backslash.
    TEST_SECTION("names");
    const std::string anna = "\xD0\x90\xD0\xBD\xD0\xBD\xD0\xB0";  // Cyrillic, 4 code points
    const std::string dagger = "\xF0\x9F\x97\xA1";                  // one 4-byte code point
    const std::string tab = "Kes\ttrel", del = "Kes\x7F";
    const std::string truncated = "Ke\xD0", overlong = "\xC0\x80", stray = "\x80Ke";
    const std::string longCyrillic = repeat("\xD0\x90", kMaxNameCodepoints);  // 48 bytes
    CHECK_EQ(nameCodepoints("Kestrel"), 7);
    CHECK_EQ(nameCodepoints(anna), 4);
    CHECK_EQ(nameCodepoints(dagger), 1);
    CHECK_EQ(nameCodepoints(tab), -1);
    CHECK_EQ(nameCodepoints(del), -1);
    CHECK_EQ(nameCodepoints(truncated), -1);
    CHECK_EQ(nameCodepoints(overlong), -1);
    CHECK_EQ(nameCodepoints(stray), -1);
    CHECK(validate(named(anna)) == CreationError::None);
    CHECK(validate(named(longCyrillic)) == CreationError::None);
    CHECK(validate(named(repeat("a", kMaxNameCodepoints))) == CreationError::None);
    CHECK(validate(named(repeat("a", kMaxNameCodepoints + 1))) == CreationError::NameTooLong);
    CHECK(validate(named(" Kestrel")) == CreationError::BadName);
    CHECK(validate(named("Kestrel ")) == CreationError::BadName);
    CHECK(validate(named(tab)) == CreationError::BadName);
    CHECK(validate(named(truncated)) == CreationError::BadName);

    TEST_SECTION("appearance");
    CharacterSheet c = named("Kestrel");
    c.facePalette = static_cast<std::uint8_t>(kFacePaletteCount - 1);
    CHECK(validate(c) == CreationError::None);
    c.facePalette = kFacePaletteCount;
    CHECK(validate(c) == CreationError::BadAppearance);
    c.facePalette = 0;
    c.silhouette = static_cast<BodySilhouette>(kSilhouetteNames.size());
    CHECK(validate(c) == CreationError::BadAppearance);
    c.silhouette = BodySilhouette::Heavy;
    c.voice = static_cast<VoiceSet>(kVoiceNames.size());
    CHECK(validate(c) == CreationError::BadAppearance);
    c.voice = VoiceSet::Rasp;
    c.cloak = static_cast<CloakStyle>(kCloakNames.size());
    CHECK(validate(c) == CreationError::BadAppearance);
    c.cloak = CloakStyle::Hooded;
    CHECK(validate(c) == CreationError::None);

    TEST_SECTION("scars");
    CHECK_EQ(scarCount(0), 0);
    CHECK_EQ(scarCount(0xFF), static_cast<int>(kScarSites.size()));
    c.scars = 0b00000111;
    CHECK(validate(c) == CreationError::None);
    c.scars = 0b10010011;
    CHECK(validate(c) == CreationError::TooManyScars);
    c.scars = 0b10000001;  // burn + brow
    CHECK(validate(c) == CreationError::None);

    TEST_SECTION("choices");
    for (std::size_t i = 0; i < kStartClassCount; ++i) {
        c.cls = static_cast<StartClass>(i);
        CHECK(validate(c) == CreationError::None);
    }
    c.cls = static_cast<StartClass>(kStartClassCount);
    CHECK(validate(c) == CreationError::BadChoice);
    c.cls = StartClass::Ranger;
    c.city = static_cast<StartingCity>(kStartingCityIds.size());
    CHECK(validate(c) == CreationError::BadChoice);
    c.city = StartingCity::KharDuun;
    c.origin = static_cast<OriginMemory>(kOriginNames.size());
    CHECK(validate(c) == CreationError::BadChoice);
    c.origin = OriginMemory::GroveFoundling;
    CHECK(validate(c) == CreationError::None);

    TEST_SECTION("city ids");
    CHECK(cityId(StartingCity::Aurelia) == "city.aurelia");
    CHECK(cityId(StartingCity::VerdantCourt) == "city.verdant_court");
    CHECK(cityId(static_cast<StartingCity>(kStartingCityIds.size())) == "unknown");
    for (std::size_t i = 0; i < kStartingCityIds.size(); ++i) {
        const std::string_view id = cityId(static_cast<StartingCity>(i));
        CHECK(id.substr(0, 5) == "city.");
        for (std::size_t k = 0; k < i; ++k) CHECK(id != kStartingCityIds[k]);
    }

    TEST_SECTION("origin reveal");
    c.origin = OriginMemory::OrderDeserter;
    CHECK_EQ(factionJudgement(c, Faction::OrderOfThaumiel), 0);  // hidden origins are neutral
    CHECK(revealOrigin(c));
    CHECK(!revealOrigin(c));  // later shrines are lore, not a second judgement
    CHECK(c.originRevealed);
    CHECK_EQ(factionJudgement(c, Faction::OrderOfThaumiel), -150);
    CHECK_EQ(factionJudgement(c, Faction::VeyrSmugglers), 100);
    CHECK_EQ(factionJudgement(c, Faction::MourgeCults), 0);
    for (std::size_t o = 0; o < kOriginNames.size(); ++o) {
        int gains = 0;
        int losses = 0;
        for (std::size_t f = 0; f < kFactionCount; ++f) {
            const int d = originJudgement(static_cast<OriginMemory>(o), static_cast<Faction>(f));
            gains += d > 0 ? 1 : 0;
            losses += d < 0 ? 1 : 0;
            CHECK(d >= -1000 && d <= 1000);
        }
        CHECK_EQ(gains, 1);   // every origin wins one faction's trust...
        CHECK_EQ(losses, 1);  // ...and costs another's
    }

    TEST_SECTION("equality");
    CharacterSheet a = named(anna);
    CharacterSheet b = a;
    CHECK(a == b);
    b.scars = 1;
    CHECK(!(a == b));

    return ::lt::test::finish("character");
}

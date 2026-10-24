// Section 8 crafting loop tests: device gating, recipe unlocks, aspect costs, flux risk,
// failure-as-content outcomes with salvage, Thaumonomicon records and seeded replay (Section 19).
#include <algorithm>
#include <cstdint>
#include <string>

#include "TestHarness.h"
#include "game/Crafting.h"

using namespace lt::game;
using lt::world::Aspect;
using lt::world::AspectLink;
using lt::world::ResearchChain;
using lt::world::ResearchError;
using lt::world::ResearchNode;

namespace {

const std::string kAltar = "research.infusion_altar";
const std::string kAether = "research.aether_core";

ResearchChain makeResearch() {
    ResearchChain c;
    c.addTarget({"material.glass_shard", AspectPool{2, 2, 2, 2, 2, 2}});
    ResearchNode altar;
    altar.id = kAltar;
    altar.cost = AspectPool{1, 1, 0, 0, 0, 0};
    altar.requiredLinks = {{Aspect::Motion, Aspect::Form}};
    altar.unlock = "device.infusion_altar";
    c.addNode(altar);
    ResearchNode aether;
    aether.id = kAether;
    aether.prerequisites = {kAltar};
    aether.cost = AspectPool{0, 0, 0, 0, 1, 1};
    aether.requiredLinks = {{Aspect::Light, Aspect::Memory}};
    aether.unlock = "recipe.aether_core";
    c.addNode(aether);
    return c;
}

bool study(ResearchChain& c, const std::string& node, AspectLink link) {
    return c.begin(node) == ResearchError::None && c.placeLink(node, link) == ResearchError::None &&
           c.solve(node) == ResearchError::None;
}

ItemCatalog makeCatalog() {
    ItemCatalog cat;
    ItemDef core;
    core.id = "attachment.aether_core";
    core.kind = ItemKind::Attachment;
    core.socketType = "core";
    core.weight = 0.5f;
    core.maxDurability = 60;
    core.aspectTags = {"motion", "light"};
    cat.add(core);
    return cat;
}

InfusionRecipe aetherRecipe() {
    InfusionRecipe r;
    r.id = "infusion.aether_core";
    r.outputDef = "attachment.aether_core";
    r.cost = AspectPool{2, 0, 0, 0, 2, 2};
    r.flux = 12;
    r.risk = 0;
    r.requiredUnlock = "recipe.aether_core";
    return r;
}

} // namespace

int main() {
    const ItemCatalog catalog = makeCatalog();
    const InfusionRecipe recipe = aetherRecipe();

    TEST_SECTION("devices");
    CHECK(std::string(toString(Device::InfusionAltar)) == "device.infusion_altar");
    CHECK(std::string(toString(Device::MobileFieldKit)) == "device.mobile_field_kit");
    CHECK(std::string(toString(Device::Count)) == "unknown");
    ResearchChain research = makeResearch();
    CHECK(hasDevice(research, Device::ResearchTable));
    CHECK(!hasDevice(research, Device::InfusionAltar));

    TEST_SECTION("errors leave inputs untouched");
    Inventory inv(catalog);
    FluxField flux;
    CHECK(research.scan("material.glass_shard"));
    const AspectPool scanned = research.aspectPool();
    InfusionResult r = infuse(research, catalog, inv, recipe, flux, 1);
    CHECK(r.error == CraftError::MissingDevice);
    CHECK(research.aspectPool() == scanned);
    CHECK(study(research, kAltar, {Aspect::Motion, Aspect::Form}));
    CHECK(hasDevice(research, Device::InfusionAltar));
    CHECK(!hasDevice(research, Device::FluxCondenser));
    CHECK(infuse(research, catalog, inv, recipe, flux, 1).error == CraftError::Locked);
    CHECK(study(research, kAether, {Aspect::Light, Aspect::Memory}));
    InfusionRecipe ghost = recipe;
    ghost.outputDef = "attachment.missing";
    CHECK(infuse(research, catalog, inv, ghost, flux, 1).error == CraftError::UnknownOutput);
    const AspectPool unlocked = research.aspectPool();  // {1, 1, 2, 2, 1, 1}: short of the cost
    r = infuse(research, catalog, inv, recipe, flux, 1);
    CHECK(r.error == CraftError::InsufficientAspects);
    CHECK(research.aspectPool() == unlocked);
    CHECK(flux.level == 0);
    CHECK(r.itemUid == 0);
    CHECK(research.thaumonomicon().size() == 2);  // only the two research unlocks

    TEST_SECTION("successful infusion");
    research.grant(AspectPool{4, 0, 0, 0, 4, 4});
    r = infuse(research, catalog, inv, recipe, flux, 7);
    CHECK(r.error == CraftError::None);
    CHECK(r.outcome == InfusionOutcome::Success);
    CHECK(r.failChance == 0);
    CHECK(flux.level == 12);
    const AspectPool spent{3, 1, 2, 2, 3, 3};
    CHECK(research.aspectPool() == spent);
    const ItemInstance* made = inv.find(r.itemUid);
    CHECK(made != nullptr);
    if (made != nullptr) {
        CHECK(made->defId == "attachment.aether_core");
        CHECK(made->seed == 7);
        CHECK(made->provenance == "infusion:infusion.aether_core");
        CHECK(made->history.back() == "infused:infusion.aether_core");
    }
    CHECK(research.isUnlocked("infusion.aether_core.success"));

    TEST_SECTION("workshop flux carries over");
    research.grant(recipe.cost);
    r = infuse(research, catalog, inv, recipe, flux, 8);
    CHECK(r.error == CraftError::None);
    CHECK(r.failChance == 3);  // 12 flux left by the first attempt / 4
    CHECK(flux.level >= 24);
    CHECK(research.isUnlocked(recipe.id + "." + toString(r.outcome)));

    // Each attempt below runs in a fresh, fully researched workshop, so its outcome depends only on
    // the recipe, the seed and the starting flux.
    struct Attempt {
        InfusionResult result;
        std::uint32_t fluxAfter = 0;
        AspectPool poolBefore{};
        AspectPool poolAfter{};
        bool hasItem = false;
        ItemInstance item{};
        bool recorded = false;
    };
    const auto attempt = [&catalog](const InfusionRecipe& rec, std::uint64_t seed, std::uint32_t startFlux) {
        ResearchChain rc = makeResearch();
        (void)rc.scan("material.glass_shard");
        (void)study(rc, kAltar, {Aspect::Motion, Aspect::Form});
        (void)study(rc, kAether, {Aspect::Light, Aspect::Memory});
        rc.grant(rec.cost);
        Inventory bench(catalog);
        FluxField field{startFlux};
        Attempt a;
        a.poolBefore = rc.aspectPool();
        a.result = infuse(rc, catalog, bench, rec, field, seed);
        a.fluxAfter = field.level;
        a.poolAfter = rc.aspectPool();
        const ItemInstance* it = a.result.itemUid != 0 ? bench.find(a.result.itemUid) : nullptr;
        if (it != nullptr) {
            a.hasItem = true;
            a.item = *it;
        }
        a.recorded = rc.isUnlocked(rec.id + "." + toString(a.result.outcome));
        return a;
    };

    TEST_SECTION("flux escalates risk");
    InfusionRecipe risky = recipe;
    risky.risk = 30;
    const Attempt hot = attempt(risky, 11, 100);
    CHECK(hot.result.error == CraftError::None);
    CHECK(hot.result.failChance == 55);  // 30% base + 100 flux / 4
    CHECK(hot.fluxAfter == (hot.result.outcome == InfusionOutcome::HostileConstruct ? 124u : 112u));
    risky.risk = 200;
    CHECK(attempt(risky, 11, 0).result.failChance == kMaxFailChance);
    risky.risk = 0;
    CHECK(attempt(risky, 11, 1000).result.failChance == kMaxFailChance);
    CHECK(attempt(risky, 11, 0).result.outcome == InfusionOutcome::Success);

    TEST_SECTION("seeded replay");
    InfusionRecipe doomed = recipe;
    doomed.risk = 100;  // clamps to kMaxFailChance, so most attempts take a failure branch
    for (const std::uint64_t seed : {3ull, 42ull, 977ull}) {
        const Attempt x = attempt(doomed, seed, 40);
        const Attempt y = attempt(doomed, seed, 40);
        CHECK(x.result.outcome == y.result.outcome);
        CHECK(x.result.failChance == y.result.failChance);
        CHECK(x.result.mutation == y.result.mutation);
        CHECK(x.result.construct == y.result.construct);
        CHECK(x.fluxAfter == y.fluxAfter);
        CHECK(x.poolAfter == y.poolAfter);
        CHECK(x.hasItem == y.hasItem);
        CHECK(x.item.provenance == y.item.provenance);
        CHECK(x.item.history == y.item.history);
        CHECK(x.item.aspectTags == y.item.aspectTags);
    }

    return ::lt::test::finish("crafting");
}

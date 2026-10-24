#pragma once
// ThaumtMourge crafting loop (design doc Section 8): scan -> extract aspects -> refine Taumium ->
// risk flux during infusion -> install attachment -> field test -> record in the Thaumonomicon.
// Failure is content, not a reload tax: a failed infusion salvages half its aspects and yields a
// temporary mutation, a hostile construct or a unique item. Seeded, so replays match (Section 19).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>

#include "game/Equipment.h"
#include "tcore/Random.h"
#include "tworld/Research.h"

namespace lt::game {

using world::AspectPool;

// Section 8 devices. All but the research table are unlocked by a research node whose unlock
// key is the device id.
enum class Device : std::uint8_t {
    ResearchTable = 0, InfusionAltar, FluxCondenser, AuraExtractor, GolemWorkbench, PlanarLens,
    NodeStabiliser, TeleportBeacon, WeatherEngine, MobileFieldKit, Count
};
inline constexpr const char* kDeviceIds[] = {
    "device.research_table", "device.infusion_altar", "device.flux_condenser", "device.aura_extractor",
    "device.golem_workbench", "device.planar_lens", "device.node_stabiliser", "device.teleport_beacon",
    "device.weather_engine", "device.mobile_field_kit"};
static_assert(std::size(kDeviceIds) == static_cast<std::size_t>(Device::Count));

inline const char* toString(Device d) noexcept {
    return d < Device::Count ? kDeviceIds[static_cast<std::size_t>(d)] : "unknown";
}
inline bool hasDevice(const world::ResearchChain& research, Device d) {
    return d == Device::ResearchTable || (d < Device::Count && research.isUnlocked(toString(d)));
}

// Workshop flux (Section 7): every infusion attempt pollutes and raises the risk of the next.
struct FluxField { std::uint32_t level = 0; };

struct InfusionRecipe {
    std::string id;              // stable key, e.g. infusion.aether_core
    std::string outputDef;       // item definition produced
    AspectPool cost{};           // aspects consumed by every attempt
    std::uint32_t flux = 10;     // flux released into the workshop per attempt
    std::uint32_t risk = 10;     // base failure chance, percent
    std::string requiredUnlock;  // Thaumonomicon unlock gating the recipe (empty = none)
};

enum class InfusionOutcome : std::uint8_t { Success = 0, Mutation, HostileConstruct, UniqueItem };
enum class CraftError : std::uint8_t { None = 0, MissingDevice, Locked, UnknownOutput, InsufficientAspects };

inline const char* toString(InfusionOutcome o) noexcept {
    static constexpr const char* kNames[] = {"success", "mutation", "hostile_construct", "unique_item"};
    const auto i = static_cast<std::size_t>(o);
    return i < std::size(kNames) ? kNames[i] : "unknown";
}

inline constexpr std::uint32_t kMaxFailChance = 95;  // a saturated workshop still succeeds 5% of the time

struct InfusionResult {
    CraftError error = CraftError::None;
    InfusionOutcome outcome = InfusionOutcome::Success;
    std::uint32_t failChance = 0;  // percent the attempt rolled against
    std::uint64_t itemUid = 0;     // item added to the inventory (Success or UniqueItem)
    std::string mutation;          // temporary player mutation id (Mutation)
    std::string construct;         // hostile construct enemy id to spawn (HostileConstruct)
};

// Attempts an infusion at the altar. On any error every input is left untouched.
inline InfusionResult infuse(world::ResearchChain& research, const ItemCatalog& catalog, Inventory& inventory,
                             const InfusionRecipe& recipe, FluxField& flux, std::uint64_t seed) {
    InfusionResult r;
    const ItemDef* out = catalog.find(recipe.outputDef);
    if (!hasDevice(research, Device::InfusionAltar)) r.error = CraftError::MissingDevice;
    else if (!recipe.requiredUnlock.empty() && !research.isUnlocked(recipe.requiredUnlock)) r.error = CraftError::Locked;
    else if (out == nullptr) r.error = CraftError::UnknownOutput;
    else if (!research.spend(recipe.cost)) r.error = CraftError::InsufficientAspects;
    if (r.error != CraftError::None) return r;

    core::Rng rng(core::hashCombine(seed, detail::idHash(recipe.id)));
    r.failChance = std::min<std::uint32_t>(kMaxFailChance, recipe.risk + flux.level / 4);
    flux.level += recipe.flux;
    if (rng.below(100) >= r.failChance) {
        ItemInstance it = generateItem(*out, seed, "infusion:" + recipe.id);
        pushHistory(it, "infused:" + recipe.id);
        r.itemUid = inventory.add(std::move(it));
    } else {
        AspectPool salvage{};
        std::size_t dominant = 0;
        for (std::size_t i = 0; i < world::kPrimalAspectCount; ++i) {
            salvage[i] = recipe.cost[i] / 2;
            if (recipe.cost[i] > recipe.cost[dominant]) dominant = i;
        }
        research.grant(salvage);
        r.outcome = static_cast<InfusionOutcome>(1 + rng.below(3));
        if (r.outcome == InfusionOutcome::Mutation) {
            r.mutation = std::string("mutation.") + world::toString(static_cast<world::Aspect>(dominant));
        } else if (r.outcome == InfusionOutcome::HostileConstruct) {
            r.construct = "enemy.flux_construct";
            flux.level += recipe.flux;  // the construct vents a second surge as it wakes
        } else {
            ItemInstance it = generateItem(*out, seed, "failed_infusion:" + recipe.id);
            it.rarity = Rarity::Mythic;
            if (!detail::contains(it.aspectTags, std::string("flux"))) it.aspectTags.push_back("flux");
            pushHistory(it, "flux_scarred:" + recipe.id);
            r.itemUid = inventory.add(std::move(it));
        }
    }
    research.record(recipe.id + "." + toString(r.outcome));
    return r;
}

} // namespace lt::game

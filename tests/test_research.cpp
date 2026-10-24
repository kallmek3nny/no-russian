// Milestone B research tests: scanning, node-link puzzle rules, prerequisites,
// aspect costs, Thaumonomicon unlocks, and progress save/reload.
#include <string>

#include "TestHarness.h"
#include "tworld/Research.h"

using namespace lt::world;

namespace {

AspectPool pool(std::uint32_t motion, std::uint32_t form, std::uint32_t life,
                std::uint32_t decay, std::uint32_t light, std::uint32_t memory) {
    return AspectPool{motion, form, life, decay, light, memory};
}

void define(ResearchChain& c) {
    c.addTarget({"material.glass_shard", pool(2, 0, 0, 0, 3, 0)});
    c.addTarget({"creature.wisp", pool(0, 0, 2, 0, 0, 1)});

    ResearchNode lens;
    lens.id = "research.focusing_lens";
    lens.cost = pool(1, 0, 0, 0, 2, 0);
    lens.requiredLinks = {{Aspect::Light, Aspect::Motion}};
    lens.unlock = "device.focusing_lens";
    c.addNode(lens);

    ResearchNode flux;
    flux.id = "research.flux_condenser";
    flux.prerequisites = {"research.focusing_lens"};
    flux.cost = pool(0, 0, 1, 0, 0, 1);
    flux.requiredLinks = {{Aspect::Life, Aspect::Memory}, {Aspect::Memory, Aspect::Light}};
    flux.unlock = "device.flux_condenser";
    c.addNode(flux);
}

} // namespace

int main() {
    const std::string lens = "research.focusing_lens";
    const std::string flux = "research.flux_condenser";

    TEST_SECTION("aspect names");
    CHECK(std::string(toString(Aspect::Motion)) == "motion");
    CHECK(std::string(toString(Aspect::Memory)) == "memory");

    ResearchChain chain;
    define(chain);

    TEST_SECTION("scanning");
    CHECK(chain.scan("material.glass_shard"));
    CHECK(!chain.scan("material.glass_shard"));
    CHECK(!chain.scan("material.unknown"));
    CHECK(chain.aspect(Aspect::Light) == 3);
    CHECK(chain.aspect(Aspect::Motion) == 2);

    TEST_SECTION("begin rules");
    CHECK(chain.begin("research.missing") == ResearchError::UnknownNode);
    CHECK(chain.begin(flux) == ResearchError::MissingPrerequisite);
    CHECK(chain.placeLink(lens, {Aspect::Light, Aspect::Motion}) == ResearchError::NotStarted);
    CHECK(chain.solve(lens) == ResearchError::NotStarted);
    CHECK(chain.begin(lens) == ResearchError::None);
    CHECK(chain.aspect(Aspect::Light) == 1);
    CHECK(chain.aspect(Aspect::Motion) == 1);
    CHECK(chain.begin(lens) == ResearchError::None);
    CHECK(chain.aspect(Aspect::Light) == 1);  // no double charge

    TEST_SECTION("puzzle");
    CHECK(chain.placeLink(lens, {Aspect::Life, Aspect::Decay}) == ResearchError::InvalidLink);
    CHECK(chain.solve(lens) == ResearchError::PuzzleIncomplete);
    CHECK(chain.placeLink(lens, {Aspect::Motion, Aspect::Light}) == ResearchError::None);  // reversed ok
    CHECK(chain.placeLink(lens, {Aspect::Light, Aspect::Motion}) == ResearchError::None);  // idempotent
    CHECK(chain.solve(lens) == ResearchError::None);
    CHECK(chain.isComplete(lens));
    CHECK(chain.isUnlocked("device.focusing_lens"));
    CHECK(chain.solve(lens) == ResearchError::AlreadyComplete);
    CHECK(chain.begin(lens) == ResearchError::AlreadyComplete);

    TEST_SECTION("chain continues");
    CHECK(chain.begin(flux) == ResearchError::InsufficientAspects);
    CHECK(chain.scan("creature.wisp"));
    CHECK(chain.begin(flux) == ResearchError::None);
    CHECK(chain.aspect(Aspect::Life) == 1);
    CHECK(chain.aspect(Aspect::Memory) == 0);
    CHECK(chain.placeLink(flux, {Aspect::Life, Aspect::Memory}) == ResearchError::None);
    CHECK(chain.solve(flux) == ResearchError::PuzzleIncomplete);

    TEST_SECTION("save/reload mid-puzzle");
    const std::string saved = chain.serialize();
    ResearchChain reloaded;
    define(reloaded);
    CHECK(reloaded.deserialize(saved));
    CHECK(reloaded.serialize() == saved);
    CHECK(reloaded.isComplete(lens));
    CHECK(!reloaded.isComplete(flux));
    CHECK(!reloaded.scan("creature.wisp"));
    CHECK(reloaded.placeLink(flux, {Aspect::Light, Aspect::Memory}) == ResearchError::None);
    CHECK(reloaded.solve(flux) == ResearchError::None);
    CHECK(reloaded.isUnlocked("device.flux_condenser"));
    CHECK(reloaded.thaumonomicon().size() == 2);

    TEST_SECTION("rejects bad data");
    const std::string before = reloaded.serialize();
    CHECK(!reloaded.deserialize("garbage"));
    CHECK(!reloaded.deserialize("research_v1\n1 2"));
    CHECK(!reloaded.deserialize("research_v1\n0 0 0 0 0 0\n0\n0\n0\n0\n1\nresearch.x 9 0\n"));
    CHECK(reloaded.serialize() == before);

    return ::lt::test::finish("research");
}

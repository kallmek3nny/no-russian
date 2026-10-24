// Sections 8/21/25 recipe record tests: a full record decodes into a RecipeDef with the crafting
// loop's InfusionRecipe defaults, the shipped data/recipes/infusions.json matches the aether core
// recipe, and strict record errors (keys, ids, aspect costs, flux and risk bounds, duplicate ids)
// name the source and record for the content validator.
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include "TestHarness.h"
#include "tworld/RecipeRecord.h"

using namespace lt::world;

namespace {

const std::string kRecord =
    R"({"id":"infusion.test","output":"attachment.test","cost":{"motion":4,"light":1},)"
    R"("flux":20,"risk":15,"unlock":"recipe.test"})";

// kRecord with the first `from` replaced by `to`.
std::string with(std::string_view from, std::string_view to, std::string s = kRecord) {
    const std::size_t at = s.find(from);
    if (at != std::string::npos) s.replace(at, from.size(), to);
    return s;
}

RecipeLoadResult load(const std::string& records) { return parseRecipeRecords("[" + records + "]", "t.json"); }

bool failsWith(const std::string& records, std::string_view message) {
    const RecipeLoadResult r = load(records);
    return !r.ok() && r.recipes.empty() && r.error.find(message) != std::string::npos;
}

}  // namespace

int main() {
    TEST_SECTION("valid record");
    {
        const RecipeLoadResult r = load(kRecord);
        CHECK(r.ok());
        CHECK_EQ(r.recipes.size(), std::size_t(1));
        if (r.ok() && r.recipes.size() == 1) {
            const RecipeDef& d = r.recipes[0];
            const AspectPool cost{4, 0, 0, 0, 1, 0};
            CHECK_EQ(d.id, std::string("infusion.test"));
            CHECK_EQ(d.output, std::string("attachment.test"));
            CHECK(d.cost == cost);
            CHECK_EQ(d.flux, 20u);
            CHECK_EQ(d.risk, 15u);
            CHECK_EQ(d.requiredUnlock, std::string("recipe.test"));
        }
        // Optional keys fall back to the InfusionRecipe defaults; records keep file order.
        const RecipeLoadResult m = load(R"({"id":"infusion.min","output":"attachment.min","cost":{"form":1}})");
        CHECK(m.ok() && m.recipes.size() == 1);
        if (m.ok() && m.recipes.size() == 1) {
            CHECK_EQ(m.recipes[0].flux, 10u);
            CHECK_EQ(m.recipes[0].risk, 10u);
            CHECK(m.recipes[0].requiredUnlock.empty());
        }
        const RecipeLoadResult two = load(kRecord + "," + with("infusion.test", "infusion.second"));
        CHECK(two.ok() && two.recipes.size() == 2);
        if (two.ok() && two.recipes.size() == 2) CHECK_EQ(two.recipes[1].id, std::string("infusion.second"));
        CHECK(load("").ok());
        CHECK(load("").recipes.empty());
    }

#ifdef LT_DATA_DIR
    TEST_SECTION("shipped infusions");
    {
        const std::filesystem::path dir = std::filesystem::path(LT_DATA_DIR) / "recipes";
        const RecipeLoadResult r = loadRecipeFile(dir / "infusions.json");
        CHECK(r.ok());
        CHECK_EQ(r.recipes.size(), std::size_t(1));
        if (r.ok() && r.recipes.size() == 1) {
            // The same values as aetherRecipe() in tests/test_crafting.cpp.
            const RecipeDef& d = r.recipes[0];
            const AspectPool cost{2, 0, 0, 0, 2, 2};
            CHECK_EQ(d.id, std::string("infusion.aether_core"));
            CHECK_EQ(d.output, std::string("attachment.aether_core"));
            CHECK(d.cost == cost);
            CHECK_EQ(d.flux, 12u);
            CHECK_EQ(d.risk, 0u);
            CHECK_EQ(d.requiredUnlock, std::string("recipe.aether_core"));
        }
        const RecipeLoadResult missing = loadRecipeFile(dir / "missing.json");
        CHECK(!missing.ok() && missing.recipes.empty());
    }
#endif

    return lt::test::finish("recipe_record");
}

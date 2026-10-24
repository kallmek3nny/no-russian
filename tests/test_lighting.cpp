// TLight (Sections 15, 16, 23): per-tier lighting pass lists derived from the TRender feature
// table, canonical pass ordering, and the section 16 emissive colour language.
#include <cstddef>
#include <initializer_list>
#include <vector>

#include "TestHarness.h"
#include "tlight/Lighting.h"
#include "trender/RenderTier.h"

using namespace lt::light;
using lt::render::RenderTier;

namespace {

// Passes must appear in strictly increasing enum order (the execution order).
bool ordered(const LightingConfig& c) {
    for (std::size_t i = 1; i < c.passes.size(); ++i)
        if (static_cast<int>(c.passes[i - 1]) >= static_cast<int>(c.passes[i])) return false;
    return true;
}

// TLight must never enable a feature the TRender tier forbids, nor drop one it requires.
bool matchesTier(RenderTier t) {
    const LightingConfig c = buildLighting(t);
    const lt::render::TierFeatures f = lt::render::featuresFor(t);
    return c.hdr == f.hdr && c.has(Pass::ShadowMap) == f.shadows &&
           c.has(Pass::PaletteLighting) == f.paletteLighting && c.has(Pass::ContactAO) == f.contactAO &&
           c.has(Pass::VolumetricFog) == f.volumetricLighting &&
           c.has(Pass::PlanarReflection) == f.planarReflections && c.has(Pass::GodRays) == f.godRays &&
           c.has(Pass::Bloom) == f.bloom && c.maxShadowCasters == f.maxShadowCasters &&
           c.maxReflectionProbes == f.maxReflectionProbes;
}

}  // namespace

int main() {
    TEST_SECTION("desktop ultra pipeline");
    const LightingConfig u = buildLighting(RenderTier::DesktopUltra);
    const std::vector<Pass> ultra{Pass::ShadowMap, Pass::ProbeLighting, Pass::ContactAO,
                                  Pass::VolumetricFog, Pass::PlanarReflection, Pass::GodRays,
                                  Pass::Bloom, Pass::Tonemap, Pass::StepQuantize};
    CHECK(u.tier == RenderTier::DesktopUltra);
    CHECK(u.passes == ultra);
    CHECK(u.hdr);
    CHECK_EQ(u.fogSlices, 64);
    CHECK_EQ(u.godRaySamples, 32);
    CHECK_EQ(u.bloomMips, 6);
    CHECK_EQ(u.highlightSteps, 6);
    CHECK(!u.has(Pass::DistanceFog) && !u.has(Pass::SimplifiedReflection));

    TEST_SECTION("desktop medium pipeline");
    const LightingConfig m = buildLighting(RenderTier::DesktopMedium);
    const std::vector<Pass> medium{Pass::ShadowMap, Pass::ProbeLighting, Pass::DistanceFog,
                                   Pass::SimplifiedReflection, Pass::Bloom, Pass::Tonemap,
                                   Pass::StepQuantize};
    CHECK(m.passes == medium);
    CHECK(m.hdr);
    CHECK_EQ(m.fogSlices, 0);
    CHECK_EQ(m.godRaySamples, 0);
    CHECK_EQ(m.bloomMips, 4);
    CHECK(!m.has(Pass::VolumetricFog) && !m.has(Pass::PlanarReflection) && !m.has(Pass::GodRays));

    TEST_SECTION("android c31 pipeline");
    const LightingConfig a = buildLighting(RenderTier::AndroidC31);
    const std::vector<Pass> android{Pass::ShadowMap, Pass::PaletteLighting, Pass::ProbeLighting,
                                    Pass::DistanceFog, Pass::Tonemap, Pass::StepQuantize};
    CHECK(a.passes == android);
    CHECK(!a.hdr);
    CHECK_EQ(a.bloomMips, 0);
    CHECK_EQ(a.fogSlices, 0);
    CHECK_EQ(a.godRaySamples, 0);
    CHECK_EQ(a.maxShadowCasters, 4);
    CHECK_EQ(a.maxReflectionProbes, 1);
    CHECK_EQ(a.highlightSteps, 4);
    CHECK(!a.has(Pass::PlanarReflection) && !a.has(Pass::Bloom));

    TEST_SECTION("tier agreement and determinism");
    for (const RenderTier t : {RenderTier::DesktopUltra, RenderTier::DesktopMedium, RenderTier::AndroidC31}) {
        const LightingConfig c = buildLighting(t);
        CHECK(ordered(c));
        CHECK(matchesTier(t));
        CHECK(!c.passes.empty() && c.passes.back() == Pass::StepQuantize);
        CHECK(buildLighting(t).passes == c.passes);
    }

    TEST_SECTION("emissive colour language");
    const Rgb tau = emissiveColour(Emissive::Taumium);
    CHECK(tau.g > 0.6f && tau.b > 0.6f && tau.r < 0.3f);  // cyan/teal
    const Rgb flux = emissiveColour(Emissive::Flux);
    CHECK(flux.r > flux.g && flux.b > flux.g && flux.r < 0.5f && flux.b < 0.5f);  // magenta-black
    const Rgb ash = emissiveColour(Emissive::Ash);
    CHECK(ash.r > ash.g && ash.g > ash.b);  // amber
    const Rgb frost = emissiveColour(Emissive::Frost);
    CHECK(frost.b >= frost.g && frost.g >= frost.r && frost.r > 0.75f);  // blue-white
    const Rgb mem = emissiveColour(Emissive::Memory);
    CHECK(mem.r >= mem.g && mem.g > mem.b && mem.b > 0.5f);  // pale gold

    TEST_SECTION("pass names");
    CHECK(toString(Pass::ShadowMap) == "shadow_map");
    CHECK(toString(Pass::VolumetricFog) == "volumetric_fog");
    CHECK(toString(Pass::StepQuantize) == "step_quantize");

    return ::lt::test::finish("lighting");
}

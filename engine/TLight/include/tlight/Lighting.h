#pragma once
// Legend Engine T - TLight (design doc sections 15, 16, 23).
// HDR, bloom, volumetric fog, god rays, probes and planar reflection assembled per TRender
// capability tier, plus the section 16 emissive colour language, so the GL 4.0, GL 3.2 and
// GLES 3 backends all build the same pass list for a tier and agree on colour.
#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

#include "trender/RenderTier.h"

namespace lt::light {

// Passes in execution order; each tier enables a subset of them.
enum class Pass : std::uint8_t {
    ShadowMap = 0, PaletteLighting, ProbeLighting, ContactAO, VolumetricFog, DistanceFog,
    PlanarReflection, SimplifiedReflection, GodRays, Bloom, Tonemap, StepQuantize
};

constexpr std::string_view toString(Pass p) noexcept {
    switch (p) {
        case Pass::ShadowMap: return "shadow_map";
        case Pass::PaletteLighting: return "palette_lighting";
        case Pass::ProbeLighting: return "probe_lighting";
        case Pass::ContactAO: return "contact_ao";
        case Pass::VolumetricFog: return "volumetric_fog";
        case Pass::DistanceFog: return "distance_fog";
        case Pass::PlanarReflection: return "planar_reflection";
        case Pass::SimplifiedReflection: return "simplified_reflection";
        case Pass::GodRays: return "god_rays";
        case Pass::Bloom: return "bloom";
        case Pass::Tonemap: return "tonemap";
        case Pass::StepQuantize: return "step_quantize";
    }
    return "unknown";
}

struct Rgb { float r = 0.0f, g = 0.0f, b = 0.0f; };

// Section 16: Taumium cyan/teal, flux magenta-black, ash amber, frost blue-white, memory pale gold.
enum class Emissive : std::uint8_t { Taumium = 0, Flux, Ash, Frost, Memory };

constexpr Rgb emissiveColour(Emissive e) noexcept {
    switch (e) {
        case Emissive::Taumium: return {0.10f, 0.85f, 0.80f};
        case Emissive::Flux: return {0.45f, 0.02f, 0.38f};
        case Emissive::Ash: return {1.00f, 0.62f, 0.15f};
        case Emissive::Frost: return {0.82f, 0.92f, 1.00f};
        case Emissive::Memory: return {0.96f, 0.88f, 0.62f};
    }
    return {};
}

struct LightingConfig {
    render::RenderTier tier = render::RenderTier::DesktopMedium;
    std::vector<Pass> passes;  // execution order
    bool hdr = false;
    int bloomMips = 0;         // 0 when bloom is off
    int fogSlices = 0;         // volumetric froxel depth slices; 0 = analytic distance fog
    int godRaySamples = 0;
    int maxShadowCasters = 0, maxReflectionProbes = 0;
    int highlightSteps = 0;    // stepped highlight bands ("pixel-art discipline applied to 3D")
    bool has(Pass p) const noexcept { return std::find(passes.begin(), passes.end(), p) != passes.end(); }
};

// Builds the lighting pipeline for a tier from the TRender feature table, so TLight can never
// enable a feature the tier does not allow (e.g. planar reflections on Android C31).
inline LightingConfig buildLighting(render::RenderTier tier) {
    const render::TierFeatures f = render::featuresFor(tier);
    LightingConfig c;
    c.tier = tier;
    c.hdr = f.hdr;
    c.maxShadowCasters = f.maxShadowCasters;
    c.maxReflectionProbes = f.maxReflectionProbes;
    if (f.shadows) c.passes.push_back(Pass::ShadowMap);
    if (f.paletteLighting) c.passes.push_back(Pass::PaletteLighting);
    if (f.bakedProbes || f.maxReflectionProbes > 0) c.passes.push_back(Pass::ProbeLighting);
    if (f.contactAO) c.passes.push_back(Pass::ContactAO);
    if (f.volumetricLighting) {
        c.passes.push_back(Pass::VolumetricFog);
        c.fogSlices = 64;
    } else if (f.fog) {
        c.passes.push_back(Pass::DistanceFog);
    }
    if (f.planarReflections) c.passes.push_back(Pass::PlanarReflection);
    else if (f.simplifiedReflections) c.passes.push_back(Pass::SimplifiedReflection);
    if (f.godRays) {
        c.passes.push_back(Pass::GodRays);
        c.godRaySamples = 32;
    }
    if (f.bloom) {
        c.passes.push_back(Pass::Bloom);
        c.bloomMips = f.volumetricLighting ? 6 : 4;
    }
    c.passes.push_back(Pass::Tonemap);
    c.passes.push_back(Pass::StepQuantize);
    c.highlightSteps = f.paletteLighting ? 4 : 6;
    return c;
}

}  // namespace lt::light

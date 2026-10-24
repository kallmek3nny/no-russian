#pragma once
// Legend Engine T - TRender capability tiers (design doc sections 15, 19, 23).
// One simulation core, three renderer tiers: the tier is chosen from detected GPU
// capabilities and decides which lighting features and frame budgets may be used.
#include <cstdint>
#include <optional>
#include <string_view>

namespace lt::render {

enum class Backend : std::uint8_t { GL40 = 0, GL32, GLES3 };
enum class RenderTier : std::uint8_t { DesktopUltra = 0, DesktopMedium, AndroidC31 };

constexpr std::string_view toString(RenderTier t) noexcept {
    switch (t) {
        case RenderTier::DesktopUltra: return "desktop_ultra";
        case RenderTier::DesktopMedium: return "desktop_medium";
        case RenderTier::AndroidC31: return "android_c31";
    }
    return "unknown";
}

// What the platform layer reports after creating a context.
struct GpuCaps {
    bool gles = false;
    int major = 0, minor = 0;  // GL or GLES context version
    std::uint32_t vramMb = 0;  // 0 when the driver does not report it
};

struct TierFeatures {
    Backend backend = Backend::GL32;
    bool hdr = false, bloom = false, volumetricLighting = false, planarReflections = false;
    bool godRays = false, animatedGrass = false, shadows = false, cascadedShadows = false;
    bool contactAO = false, fog = false, simplifiedReflections = false;
    bool paletteLighting = false, bakedProbes = false, impostorVegetation = false;
    int targetFps = 60;
    int internalResMin = 1080, internalResDefault = 1080, internalResMax = 1080;  // render height, px
    int maxShadowCasters = 0, maxReflectionProbes = 0;
    int streamRingMin = 12, streamRingMax = 32;  // chunks (section 4.1)
};

inline TierFeatures featuresFor(RenderTier t) noexcept {
    TierFeatures f;
    switch (t) {
        case RenderTier::DesktopUltra:
            f.backend = Backend::GL40;
            f.hdr = f.bloom = f.volumetricLighting = f.planarReflections = true;
            f.godRays = f.animatedGrass = f.shadows = f.cascadedShadows = true;
            f.contactAO = f.fog = true;
            f.maxShadowCasters = 16;
            f.maxReflectionProbes = 8;
            break;
        case RenderTier::DesktopMedium:
            f.backend = Backend::GL32;
            f.hdr = f.bloom = f.shadows = f.cascadedShadows = true;
            f.fog = f.simplifiedReflections = true;
            f.maxShadowCasters = 8;
            f.maxReflectionProbes = 4;
            break;
        case RenderTier::AndroidC31:
            f.backend = Backend::GLES3;
            f.paletteLighting = f.bakedProbes = f.impostorVegetation = f.fog = true;
            f.shadows = true;  // 4 casters, no planar reflections by default (section 23)
            f.targetFps = 30;
            f.internalResMin = 512;
            f.internalResDefault = 640;  // section 23 budget; section 15 allows up to 720
            f.internalResMax = 720;
            f.maxShadowCasters = 4;
            f.maxReflectionProbes = 1;
            f.streamRingMin = 5;
            f.streamRingMax = 12;
            break;
    }
    return f;
}

// GLES 3+ -> Android tier; GL 4.0+ with >= 3 GB VRAM -> Ultra; GL 3.2+ -> Medium.
// Older contexts have no supported backend: the launcher must report that instead of a black screen.
inline std::optional<RenderTier> selectTier(const GpuCaps& c) noexcept {
    const int v = c.major * 10 + c.minor;
    if (c.gles) {
        if (v >= 30) return RenderTier::AndroidC31;
        return std::nullopt;
    }
    if (v >= 40 && c.vramMb >= 3072) return RenderTier::DesktopUltra;
    if (v >= 32) return RenderTier::DesktopMedium;
    return std::nullopt;
}

}  // namespace lt::render

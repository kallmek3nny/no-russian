// TRender capability tiers (Sections 15, 19, 23): tier selection from reported GPU caps,
// per-tier lighting features, and the Android C31 frame/resolution/streaming budget.
#include <cstdint>
#include <optional>

#include "TestHarness.h"
#include "trender/RenderTier.h"

using namespace lt::render;

namespace {

GpuCaps gl(int vmaj, int vmin, std::uint32_t vramMb) { return GpuCaps{false, vmaj, vmin, vramMb}; }
GpuCaps gles(int vmaj, int vmin) { return GpuCaps{true, vmaj, vmin, 0}; }

}  // namespace

int main() {
    TEST_SECTION("tier selection");
    CHECK(selectTier(gl(4, 6, 8192)) == RenderTier::DesktopUltra);
    CHECK(selectTier(gl(4, 0, 3072)) == RenderTier::DesktopUltra);
    CHECK(selectTier(gl(4, 0, 2048)) == RenderTier::DesktopMedium);  // GL 4 but small VRAM
    CHECK(selectTier(gl(4, 6, 0)) == RenderTier::DesktopMedium);     // driver did not report VRAM
    CHECK(selectTier(gl(3, 2, 8192)) == RenderTier::DesktopMedium);
    CHECK(!selectTier(gl(3, 1, 8192)).has_value());
    CHECK(!selectTier(gl(2, 1, 0)).has_value());
    CHECK(selectTier(gles(3, 0)) == RenderTier::AndroidC31);
    CHECK(selectTier(gles(3, 2)) == RenderTier::AndroidC31);
    CHECK(!selectTier(gles(2, 0)).has_value());

    TEST_SECTION("desktop ultra features");
    const TierFeatures u = featuresFor(RenderTier::DesktopUltra);
    CHECK(u.backend == Backend::GL40);
    CHECK(u.hdr && u.bloom && u.volumetricLighting && u.planarReflections);
    CHECK(u.godRays && u.animatedGrass && u.shadows && u.contactAO);
    CHECK(!u.paletteLighting && !u.impostorVegetation);
    CHECK_EQ(u.targetFps, 60);
    CHECK_EQ(u.streamRingMin, 12);
    CHECK_EQ(u.streamRingMax, 32);

    TEST_SECTION("desktop medium features");
    const TierFeatures m = featuresFor(RenderTier::DesktopMedium);
    CHECK(m.backend == Backend::GL32);
    CHECK(m.hdr && m.bloom && m.cascadedShadows && m.fog && m.simplifiedReflections);
    CHECK(!m.volumetricLighting && !m.planarReflections && !m.godRays);
    CHECK_EQ(m.targetFps, 60);

    TEST_SECTION("android c31 budget");
    const TierFeatures a = featuresFor(RenderTier::AndroidC31);
    CHECK(a.backend == Backend::GLES3);
    CHECK(a.paletteLighting && a.bakedProbes && a.impostorVegetation);
    CHECK(!a.hdr && !a.planarReflections && !a.volumetricLighting && !a.godRays);
    CHECK_EQ(a.targetFps, 30);
    CHECK_EQ(a.internalResMin, 512);
    CHECK_EQ(a.internalResMax, 720);
    CHECK(a.internalResDefault >= 512 && a.internalResDefault <= 640);
    CHECK_EQ(a.maxShadowCasters, 4);
    CHECK_EQ(a.maxReflectionProbes, 1);
    CHECK_EQ(a.streamRingMin, 5);
    CHECK_EQ(a.streamRingMax, 12);

    TEST_SECTION("tier names for the build log");
    CHECK(toString(RenderTier::DesktopUltra) == "desktop_ultra");
    CHECK(toString(RenderTier::DesktopMedium) == "desktop_medium");
    CHECK(toString(RenderTier::AndroidC31) == "android_c31");

    return ::lt::test::finish("render");
}

// ThaumtMourge headless launcher (Sections 14, 15, 17, 19, 23, 26): the first runnable entry point.
// Picks the render tier from the reported GPU context (an unsupported context exits with a message,
// never a black screen), loads English plus an optional target locale, and installs the crash log.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "game/PlayerSim.h"
#include "game/ProfileStore.h"
#include "tcore/Input.h"
#include "tcore/Localization.h"
#include "tcore/Log.h"
#include "trender/RenderTier.h"
#include "tworld/Biome.h"

// Section 19 portable gate: GetModuleFileNameW finds the executable, so data/ and saves/ resolve beside it.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifndef LT_DATA_DIR
#define LT_DATA_DIR "data"
#endif
// Section 20 build identity: CMake passes the git commit and content version; standalone builds still report one.
#ifndef LT_BUILD_COMMIT
#define LT_BUILD_COMMIT "unknown"
#endif
#ifndef LT_CONTENT_VERSION
#define LT_CONTENT_VERSION "0.0.0-dev"
#endif

namespace fs = std::filesystem;
using namespace lt;

namespace {

// Section 20: the build log names the commit, content version, renderer tier, seed and save schema version.
std::string buildIdentity(render::RenderTier tier, std::uint64_t seed) {
    return std::string("build: commit ") + LT_BUILD_COMMIT + " | content " + LT_CONTENT_VERSION + " | tier " +
           std::string(render::toString(tier)) + " | seed " + std::to_string(seed) + " | save schema " +
           std::to_string(game::kProfileSchemaVersion);
}

// Section 19 Windows/Linux gates: a portable build reads data/ and writes saves/ beside the executable,
// so it never depends on the source tree. LT_DATA_DIR is only a developer fallback for in-tree runs.
fs::path executableDir(const char* argv0) {
    std::error_code ec;
#if defined(_WIN32)
    std::wstring buf(MAX_PATH, L'\0');
    for (int attempt = 0; attempt < 6; ++attempt) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) break;
        if (n < buf.size()) {
            buf.resize(n);
            return fs::path(buf).parent_path();
        }
        buf.resize(buf.size() * 2);
    }
#elif defined(__linux__)
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec && !self.empty()) return self.parent_path();
    ec.clear();
#endif
    if (argv0 != nullptr && *argv0 != '\0') {
        const fs::path abs = fs::absolute(fs::path(argv0), ec);
        if (!ec) return abs.parent_path();
        ec.clear();
    }
    return fs::current_path(ec);
}

enum class DataSource { Flag, Portable, DeveloperTree };

std::string_view dataSourceName(DataSource s) {
    switch (s) {
    case DataSource::Flag: return "--data";
    case DataSource::Portable: return "portable";
    case DataSource::DeveloperTree: return "developer tree";
    }
    return "unknown";
}

struct Options {
    fs::path data, saves;
    DataSource dataSource = DataSource::Portable;
    std::string locale = loc::kSourceLocale;
    render::GpuCaps caps{false, 4, 0, 4096};
    std::uint64_t seed = 20261024;
    int ticks = 600, radius = 2;
    bool walk = false;
};

// data/ beside the executable wins when it holds loc/en.txt; otherwise the configured developer data tree.
// saves/ always defaults to beside the executable; --data and --saves override both.
void applyPortableDefaults(Options& o, const fs::path& exeDir) {
    std::error_code ec;
    const fs::path portable = exeDir / "data";
    if (fs::is_regular_file(portable / "loc" / "en.txt", ec)) {
        o.data = portable;
        o.dataSource = DataSource::Portable;
    } else {
        o.data = LT_DATA_DIR;
        o.dataSource = DataSource::DeveloperTree;
    }
    o.saves = exeDir / "saves";
}

std::optional<Options> parseArgs(int argc, char** argv) {
    Options o;
    applyPortableDefaults(o, executableDir(argc > 0 ? argv[0] : nullptr));
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--walk") { o.walk = true; continue; }
        if (i + 1 >= argc) return std::nullopt;
        const char* v = argv[++i];
        char* end = nullptr;
        if (a == "--data") {
            o.data = v;
            o.dataSource = DataSource::Flag;
        }
        else if (a == "--saves") o.saves = v;
        else if (a == "--locale") o.locale = v;
        else if (a == "--seed") o.seed = std::strtoull(v, nullptr, 10);
        else if (a == "--ticks") o.ticks = std::atoi(v);
        else if (a == "--radius") o.radius = std::atoi(v);
        else if (a == "--vram") o.caps.vramMb = static_cast<std::uint32_t>(std::strtoul(v, nullptr, 10));
        else if (a == "--gl" || a == "--gles") {
            o.caps.gles = a == "--gles";
            o.caps.major = static_cast<int>(std::strtol(v, &end, 10));
            if (*end != '.') return std::nullopt;
            o.caps.minor = static_cast<int>(std::strtol(end + 1, &end, 10));
        } else return std::nullopt;
    }
    if (o.ticks < 0 || o.radius < 1) return std::nullopt;
    return o;
}

std::optional<loc::StringTable> loadTable(const fs::path& dataDir, const std::string& locale, core::Logger& log) {
    const fs::path p = dataDir / "loc" / (locale + ".txt");
    std::ifstream f(p, std::ios::binary);
    if (!f) { log.error("loc: cannot open " + p.string()); return std::nullopt; }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    loc::StringTable t(locale);
    std::string err;
    if (!t.parse(text, &err)) { log.error("loc: " + p.string() + " " + err); return std::nullopt; }
    return t;
}

int run(const Options& o, core::Logger& log) {
    const auto tier = render::selectTier(o.caps);
    if (!tier) {
        std::fprintf(stderr, "unsupported graphics context %s %d.%d: needs OpenGL 3.2+ or OpenGL ES 3.0+\n",
                     o.caps.gles ? "GLES" : "GL", o.caps.major, o.caps.minor);
        log.error("render: unsupported graphics context");
        return 2;
    }
    const render::TierFeatures feat = render::featuresFor(*tier);
    std::printf("tier: %s (%d fps target, %dp internal)\n", std::string(render::toString(*tier)).c_str(),
                feat.targetFps, feat.internalResDefault);
    // Section 20: the run log and stdout both carry the build identity line.
    const std::string identity = buildIdentity(*tier, o.seed);
    std::printf("%s\n", identity.c_str());
    log.info(identity);
    // Section 19: report which data tree this run uses, so a portable package proves it ignores the source tree.
    const std::string dataLine = "data: " + o.data.string() + " (" + std::string(dataSourceName(o.dataSource)) +
                                 ") | saves: " + o.saves.string();
    std::printf("%s\n", dataLine.c_str());
    log.info(dataLine);

    const auto source = loadTable(o.data, loc::kSourceLocale, log);
    if (!source) { std::fprintf(stderr, "cannot load English strings from %s\n", o.data.string().c_str()); return 3; }
    loc::Localizer text(*source);
    if (o.locale != loc::kSourceLocale) {
        if (auto target = loadTable(o.data, o.locale, log)) {
            const auto issues = loc::validateTranslation(*source, *target);
            if (!issues.empty()) log.warn("loc: " + std::to_string(issues.size()) + " issue(s) in " + o.locale);
            text.setActive(std::move(*target));
        } else {
            std::fprintf(stderr, "warning: locale '%s' unavailable, using English\n", o.locale.c_str());
        }
    }
    std::printf("locale: %s | %s / %s / %s\n", text.activeLocale().c_str(), text.get("menu.new_journey").c_str(),
                text.get("menu.continue").c_str(), text.get("menu.settings").c_str());

    // Sections 14/17 profile layer: main file, then rolling backups, then tier defaults; saved back sanitized.
    const fs::path profileFile = game::profilePath(o.saves);
    const game::ProfileLoadResult prof = game::loadProfile(profileFile, *tier);
    std::printf("profile: %s%s (internal res %dp)\n",
                prof.loaded() ? (prof.source == 0 ? "loaded" : "recovered from backup") : "platform defaults",
                prof.repaired ? ", repaired" : "", prof.profile.settings.internalResolution);
    if (!game::saveProfile(profileFile, prof.profile)) {
        log.error("profile: cannot write " + profileFile.string());
        std::fprintf(stderr, "warning: cannot save profile to %s\n", profileFile.string().c_str());
    }

    // Milestone B vertical slice: Frost Crown terrain streamed around a fixed-tick player (Sections 4.2, 17, 19).
    const world::BiomeDef biome = world::frostCrown();
    game::PlayerSimConfig cfg;
    cfg.worldSeed = o.seed;
    cfg.streamRadius = std::min(o.radius, feat.streamRingMax);
    const int ground = world::surfaceHeight(biome, o.seed, 0, 0, world::kChunkHeightDesktop);
    cfg.spawn = world::Vec3{0.5, static_cast<double>(ground) + 2.0, 0.5};
    game::PlayerSim sim(cfg, world::makeBiomeGenerator(biome, o.seed), log);

    const auto map = input::ActionMap::desktopDefaults();
    input::InputState in(map);
    if (o.walk) in.onButton(input::keyboard('W'), true);
    for (int i = 0; i < o.ticks; ++i) {
        sim.tick(in);
        in.beginFrame();
    }
    in.releaseAll();

    const world::Vec3 feet = sim.controller().feet();
    std::printf("world: %s seed %llu | %llu ticks, %zu chunks loaded, %llu stream updates\n", biome.name.c_str(),
                static_cast<unsigned long long>(o.seed), static_cast<unsigned long long>(sim.ticks()),
                sim.streamer().loadedCount(), static_cast<unsigned long long>(sim.streamUpdates()));
    std::printf("player: feet (%.2f, %.2f, %.2f) %s | state hash %016llx\n", feet.x, feet.y, feet.z,
                sim.controller().grounded() ? "grounded" : "airborne",
                static_cast<unsigned long long>(sim.stateHash()));
    log.info("launcher: run complete");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const auto parsed = parseArgs(argc, argv);
    if (!parsed) {
        std::fprintf(stderr, "usage: thaumt_mourge [--data DIR] [--saves DIR] [--locale en|ru] [--gl X.Y | --gles X.Y]\n"
                             "                     [--vram MB] [--seed N] [--ticks N] [--radius N] [--walk]\n");
        return 64;
    }
    const Options& o = *parsed;
    std::error_code ec;
    fs::create_directories(o.saves, ec);
    core::Logger log;
    log.setMinLevel(core::LogLevel::Debug);
    if (!log.openFile(o.saves / "launcher.log"))
        std::fprintf(stderr, "warning: cannot open a log file in %s\n", o.saves.string().c_str());
    core::installCrashHandler(log, o.saves / "crash.txt");
    const int code = run(o, log);
    core::uninstallCrashHandler();
    return code;
}

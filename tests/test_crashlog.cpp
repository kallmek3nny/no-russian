// Milestone A crash-log tests: level filtering, bounded ring, file sink,
// crash report contents, and handler install/uninstall.
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "TestHarness.h"
#include "tcore/Log.h"

namespace {
std::string slurp(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}
bool contains(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }
} // namespace

using namespace lt::core;

int main() {
    TEST_SECTION("ring buffer");
    Logger log(4);
    log.setMinLevel(LogLevel::Info);
    log.debug("hidden");
    for (int i = 0; i < 6; ++i) log.info("line " + std::to_string(i));
    log.error("boom imminent");
    auto r = log.recent();
    CHECK(r.size() == 4);
    CHECK(!r.empty() && r.front() == "[INFO] line 3");
    CHECK(!r.empty() && r.back() == "[ERROR] boom imminent");
    CHECK(log.totalLines() == 7);

    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "thaumtmourge_crash_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    TEST_SECTION("file sink");
    const fs::path logFile = dir / "session.log";
    CHECK(log.openFile(logFile));
    log.warn("to file");
    log.closeFile();
    CHECK(contains(slurp(logFile), "[WARN] to file"));

    TEST_SECTION("crash report");
    const fs::path crash = dir / "crash.txt";
    CHECK(log.writeCrashReport(crash, "SIGSEGV"));
    const std::string report = slurp(crash);
    CHECK(contains(report, "ThaumtMourge crash report"));
    CHECK(contains(report, "reason: SIGSEGV"));
    CHECK(contains(report, "[ERROR] boom imminent"));
    CHECK(contains(report, "[WARN] to file"));
    CHECK(!contains(report, "line 2"));

    TEST_SECTION("crash handler");
    installCrashHandler(log, crash);
    CHECK(crashHandlerInstalled());
    uninstallCrashHandler();
    CHECK(!crashHandlerInstalled());

    fs::remove_all(dir);
    return lt::test::finish("crashlog");
}

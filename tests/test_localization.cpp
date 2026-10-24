// Section 26 localisation tests: stable keys, en source + ru target tables loaded
// from data/loc, fallback, formatting, UTF-8 validation, Cyrillic glyph coverage,
// short mobile combat prompts, and the CI translation validator.
#include <fstream>
#include <sstream>
#include <string>

#include "TestHarness.h"
#include "tcore/Localization.h"

using namespace lt::loc;

#ifndef LT_DATA_DIR
#define LT_DATA_DIR "data"
#endif

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool hasIssue(const std::vector<LocIssue>& v, LocIssueKind k, const std::string& key) {
    for (const auto& i : v) if (i.kind == k && i.key == key) return true;
    return false;
}

} // namespace

int main() {
    const std::string dir = LT_DATA_DIR;

    TEST_SECTION("key rules");
    CHECK(StringTable::validKey("prompt.guard"));
    CHECK(StringTable::validKey("item.aetherfang.name"));
    CHECK(!StringTable::validKey("noNamespace"));
    CHECK(!StringTable::validKey("Prompt.Guard"));
    CHECK(!StringTable::validKey(".lead"));
    CHECK(!StringTable::validKey("trail."));

    TEST_SECTION("utf8");
    CHECK(decodeUtf8("abc").has_value());
    CHECK(decodeUtf8("\xD0\xA9\xD0\x98\xD0\xA2")->size() == 3);  // Cyrillic
    CHECK(!decodeUtf8("\xD0").has_value());                          // truncated
    CHECK(!decodeUtf8("\xC0\x80").has_value());                      // overlong
    CHECK(!decodeUtf8("\xED\xA0\x80").has_value());                  // surrogate
    CHECK(!decodeUtf8("\xFF").has_value());

    TEST_SECTION("parse data files");
    const std::string enText = readFile(dir + "/loc/en.txt");
    const std::string ruText = readFile(dir + "/loc/ru.txt");
    CHECK(!enText.empty());
    CHECK(!ruText.empty());
    StringTable en(kSourceLocale), ru(kFirstTargetLocale);
    std::string err;
    CHECK(en.parse(enText, &err));
    CHECK(ru.parse(ruText, &err));
    CHECK(en.size() >= 20);
    CHECK(en.size() == ru.size());
    CHECK(*en.find("prompt.guard") == "GUARD");
    CHECK(*en.find("menu.studio") == "RDGaming Studios");

    TEST_SECTION("shipped translation validates");
    const auto issues = validateTranslation(en, ru);
    for (const auto& i : issues) std::printf("  issue %d %s %s\n", static_cast<int>(i.kind), i.key.c_str(), i.detail.c_str());
    CHECK(issues.empty());
    CHECK(validateTranslation(en, en).empty());

    TEST_SECTION("parse errors");
    StringTable bad;
    CHECK(!bad.parse("a.b=1\na.b=2\n", &err));
    CHECK(err.find("duplicate") != std::string::npos);
    CHECK(!bad.parse("novalue\n", &err));
    CHECK(!bad.parse("Bad.Key=x\n", &err));
    CHECK(!bad.parse("a.b=\xD0\n", &err));
    CHECK(bad.size() == 0);  // failed parses leave table untouched
    CHECK(bad.parse("# c\n\r\na.b=line1\\nline2\r\n", &err));
    CHECK(*bad.find("a.b") == "line1\nline2");

    TEST_SECTION("localizer fallback");
    StringTable partialRu(kFirstTargetLocale);
    partialRu.set("prompt.guard", "\xD0\x91\xD0\x9B\xD0\x9E\xD0\x9A");
    Localizer loc(en);
    CHECK(loc.activeLocale() == "en");
    CHECK(loc.get("prompt.guard") == "GUARD");
    loc.setActive(partialRu);
    CHECK(loc.activeLocale() == "ru");
    CHECK(loc.get("prompt.guard") == "\xD0\x91\xD0\x9B\xD0\x9E\xD0\x9A");
    CHECK(loc.get("prompt.scan") == "SCAN");  // falls back to source
    CHECK(loc.get("missing.key") == "missing.key");
    CHECK(loc.missingKeys().size() == 1);
    CHECK(loc.format("hud.event_alert", {"Caravan under siege"}) == "Event: Caravan under siege");
    loc.clearActive();
    CHECK(loc.activeLocale() == "en");

    TEST_SECTION("validator catches problems");
    StringTable src(kSourceLocale), tgt(kFirstTargetLocale);
    src.set("prompt.guard", "GUARD");
    src.set("hud.alert", "Event: {0}");
    src.set("menu.exit", "Exit");
    src.set("menu.title", "Title");
    tgt.set("prompt.guard", "\xD0\x91\xD0\x9B\xD0\x9E\xD0\x9A\xD0\x91\xD0\x9B\xD0\x9E\xD0\x9A\xD0\x91\xD0\x9B\xD0\x9E\xD0\x9A\xD0\x91");  // 13 cps
    tgt.set("hud.alert", "no placeholder");
    tgt.set("menu.title", "\xE4\xB8\x80");  // U+4E00 not in required coverage
    tgt.set("menu.extra", "x");
    const auto v = validateTranslation(src, tgt);
    CHECK(hasIssue(v, LocIssueKind::PromptTooLong, "prompt.guard"));
    CHECK(hasIssue(v, LocIssueKind::PlaceholderMismatch, "hud.alert"));
    CHECK(hasIssue(v, LocIssueKind::MissingInTarget, "menu.exit"));
    CHECK(hasIssue(v, LocIssueKind::UncoveredGlyph, "menu.title"));
    CHECK(hasIssue(v, LocIssueKind::ExtraInTarget, "menu.extra"));
    CHECK(v.size() == 5);

    return ::lt::test::finish("localization");
}

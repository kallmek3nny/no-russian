// TCore JSON tests (Sections 21, 25, 26): strict grammar, UTF-8 and escapes, error
// positions, depth limits, deterministic dump, round-trips, and the shipped item data.
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

#include "TestHarness.h"
#include "tcore/Json.h"
#include "tcore/JsonParser.h"

using lt::core::Json;
using lt::core::parseJson;

namespace {

bool parses(std::string_view text) { return parseJson(text).ok(); }

} // namespace

int main() {
    TEST_SECTION("scalars");
    {
        CHECK(parseJson("null").value.isNull());
        CHECK(parseJson("true").value.asBool());
        CHECK(!parseJson("false").value.asBool(true));
        CHECK_EQ(parseJson("42").value.asInt(), 42);
        CHECK_EQ(parseJson("-0.5").value.asNumber(), -0.5);
        CHECK_EQ(parseJson("1e3").value.asNumber(), 1000.0);
        CHECK_EQ(parseJson("2.5E-1").value.asNumber(), 0.25);
        CHECK_EQ(parseJson("  \"hi\"  ").value.str(), std::string("hi"));
        CHECK(parseJson("7").value.isInteger());
        CHECK(!parseJson("7.5").value.isInteger());
        CHECK(parses("-0"));
        CHECK(parses("0"));
        CHECK(parses(" \t\r\n[ ] "));
    }

    TEST_SECTION("strings, escapes and UTF-8");
    {
        // MSVC's traditional preprocessor does not escape backslashes when it stringizes a raw
        // string literal, so raw strings containing escapes stay outside the CHECK macros.
        const std::string_view escapes = R"("a\"b\\c\/d")";
        CHECK_EQ(parseJson(escapes).value.str(), std::string("a\"b\\c/d"));
        CHECK_EQ(parseJson(R"("\n\t\r\b\f")").value.str(), std::string("\n\t\r\b\f"));
        CHECK_EQ(parseJson(R"("A")").value.str(), std::string("A"));
        CHECK_EQ(parseJson(R"("a\u0000b")").value.str().size(), 3u);
        // Cyrillic (Section 26) through an escape and as raw UTF-8.
        CHECK_EQ(parseJson(R"("Щ")").value.str(), std::string("\xD0\xA9"));
        CHECK_EQ(parseJson("\"\xD0\xA9\xD0\xB8\"").value.str(), std::string("\xD0\xA9\xD0\xB8"));
        // Surrogate pair for U+1F600.
        CHECK_EQ(parseJson(R"("😀")").value.str(), std::string("\xF0\x9F\x98\x80"));
        CHECK(!parses(R"("\ud83d")"));
        CHECK(!parses(R"("\ude00")"));
        CHECK(!parses(R"("\ud83dA")"));
        CHECK(!parses(R"("\x")"));
        CHECK(!parses(R"("\u12")"));
        CHECK(!parses("\"a\nb\""));           // raw control character
        CHECK(!parses("\"\xC0\xAF\""));       // overlong encoding
        CHECK(!parses("\"\xED\xA0\x80\""));   // encoded surrogate
        CHECK(!parses("\"\xF5\x80\x80\x80\""));  // above U+10FFFF
        CHECK(!parses("\"\xD0\""));           // truncated sequence
        CHECK(!parses("\"abc"));
    }

    TEST_SECTION("containers and lookups");
    {
        const auto r = parseJson(
            R"({"id":"weapon.x","damage":{"physical":42,"aether":8},"tags":["motion","memory",3],"empty":{},"none":[]})");
        CHECK(r.ok());
        const Json& j = r.value;
        CHECK(j.isObject());
        CHECK_EQ(j.size(), 5u);
        CHECK_EQ(j["id"].str(), std::string("weapon.x"));
        CHECK_EQ(j["damage"]["physical"].asInt(), 42);
        CHECK_EQ(j["damage"].numberOr("aether", 0.0), 8.0);
        CHECK_EQ(j["damage"].numberOr("frost", -1.0), -1.0);
        CHECK(j["missing"]["deeper"][3].isNull());
        CHECK_EQ(j.stringList("tags").size(), 2u);  // non-string elements are skipped
        CHECK_EQ(j["tags"][2].asInt(), 3);
        CHECK(j["tags"][99].isNull());
        CHECK(j["empty"].isObject() && j["empty"].empty());
        CHECK(j["none"].isArray() && j["none"].empty());
        CHECK(j.contains("damage"));
        CHECK(!j.contains("frost"));
        CHECK_EQ(j.keys()[0], std::string("id"));
        CHECK_EQ(j.keys()[4], std::string("none"));
        CHECK_EQ(j.stringOr("id", "x"), std::string("weapon.x"));
        CHECK_EQ(j.stringOr("nope", "fallback"), std::string("fallback"));
        CHECK_EQ(j.intOr("nope", 7), 7);
        CHECK(j.boolOr("nope", true));
        CHECK(j["id"].isString() && !j["id"].isNumber());
    }

    TEST_SECTION("strict grammar");
    {
        CHECK(!parses(""));
        CHECK(!parses("   "));
        CHECK(!parses("[1,]"));
        CHECK(!parses(R"({"a":1,})"));
        CHECK(!parses("[1 2]"));
        CHECK(!parses(R"({"a" 1})"));
        CHECK(!parses(R"({a:1})"));
        CHECK(!parses(R"({"a":1 "b":2})"));
        CHECK(!parses("01"));
        CHECK(!parses("-"));
        CHECK(!parses("1."));
        CHECK(!parses(".5"));
        CHECK(!parses("1e"));
        CHECK(!parses("+1"));
        CHECK(!parses("NaN"));
        CHECK(!parses("Infinity"));
        CHECK(!parses("1e999"));
        CHECK(!parses("tru"));
        CHECK(!parses("nul"));
        CHECK(!parses("[1] x"));
        CHECK(!parses("// comment\n1"));
        CHECK(!parses(R"({"a":1,"a":2})"));
        CHECK(!parses("[[[["));
        CHECK(!parses("{"));
        CHECK(!parses("[1"));
    }

    TEST_SECTION("depth limit");
    {
        std::string deep(200, '[');
        deep.append(200, ']');
        const auto r = parseJson(deep);
        CHECK(!r.ok());
        CHECK(r.error.find("nesting") != std::string::npos);
        std::string fine(100, '[');
        fine.append(100, ']');
        CHECK(parses(fine));
        CHECK(!parseJson(fine, 50).ok());
    }

    TEST_SECTION("error positions");
    {
        const auto r = parseJson("{\n  \"a\": 1,\n  \"b\": tru\n}");
        CHECK(!r.ok());
        CHECK(r.value.isNull());
        CHECK_EQ(r.line, 3u);
        CHECK_EQ(r.column, 8u);
        CHECK(r.describe("items.json").rfind("items.json:3:8: ", 0) == 0);

        // Columns count code points, so Cyrillic text does not skew positions.
        const auto c = parseJson("[\"\xD0\xA9\xD0\xB8\", x]");
        CHECK_EQ(c.line, 1u);
        CHECK_EQ(c.column, 8u);

        const auto d = parseJson(R"({"a":1,"a":2})");
        CHECK_EQ(d.column, 8u);
        CHECK(d.error.find("duplicate") != std::string::npos);

        const auto u = parseJson("[\"abc");
        CHECK_EQ(u.column, 2u);
        CHECK(u.error.find("unterminated") != std::string::npos);

        CHECK(parseJson("[1,").describe().rfind("json:1:4: ", 0) == 0);
        CHECK(parseJson("true").describe().empty());
    }

    TEST_SECTION("byte order mark");
    {
        const auto r = parseJson("\xEF\xBB\xBF{\"a\":true}");
        CHECK(r.ok());
        CHECK(r.value["a"].asBool());
        CHECK_EQ(parseJson("\xEF\xBB\xBF[x]").column, 2u);
    }

    TEST_SECTION("build and dump");
    {
        Json j;
        j.set("id", "event.caravan_under_siege");
        j.set("cooldown_hours", 18);
        j.set("ratio", 0.5);
        j.set("ok", true);
        j.set("nothing", nullptr);
        Json stages;
        stages.push("rumour").push("ambush");
        j.set("stages", stages);
        j.set("text", "line\n\"quoted\"\t\x01");
        CHECK(j.isObject());
        CHECK(stages.isArray());
        CHECK_EQ(j.dump(),
                 std::string(R"({"id":"event.caravan_under_siege","cooldown_hours":18,"ratio":0.5,"ok":true,"nothing":null,"stages":["rumour","ambush"],"text":"line\n\"quoted\"\t\u0001"})"));

        Json small;
        small.set("a", 1);
        small.set("b", Json::array().push(2).push(3));
        CHECK_EQ(small.dump(2), std::string("{\n  \"a\": 1,\n  \"b\": [\n    2,\n    3\n  ]\n}"));
        CHECK_EQ(Json::object().dump(), std::string("{}"));
        CHECK_EQ(Json::array().dump(2), std::string("[]"));
        CHECK_EQ(Json(-7).dump(), std::string("-7"));
    }

    TEST_SECTION("round trip and equality");
    {
        const char* text =
            R"({"z":[1,2.25,-3e-2,{"k":"é😀"}],"a":{"b":null,"c":false}})";
        const auto first = parseJson(text);
        CHECK(first.ok());
        const auto second = parseJson(first.value.dump());
        CHECK(second.ok());
        CHECK(first.value == second.value);
        CHECK_EQ(second.value.dump(), first.value.dump());
        const auto pretty = parseJson(first.value.dump(4));
        CHECK(pretty.ok() && pretty.value == first.value);
        CHECK_EQ(first.value["z"][3]["k"].str(), std::string("\xC3\xA9\xF0\x9F\x98\x80"));
        // Object equality ignores member order; array order matters.
        CHECK(parseJson(R"({"a":1,"b":2})").value == parseJson(R"({"b":2,"a":1})").value);
        CHECK(!(parseJson("[1,2]").value == parseJson("[2,1]").value));
        CHECK(!(parseJson(R"({"a":1})").value == parseJson(R"({"a":1,"b":2})").value));
        CHECK(!(Json(1) == Json("1")));
    }

    TEST_SECTION("mutation");
    {
        Json j = Json::object();
        j.set("a", 1).set("b", 2).set("a", 3);
        CHECK_EQ(j.size(), 2u);
        CHECK_EQ(j["a"].asInt(), 3);
        CHECK_EQ(j.keys()[0], std::string("a"));
        CHECK(j.erase("a"));
        CHECK(!j.erase("a"));
        CHECK_EQ(j.size(), 1u);
        Json* b = j.find("b");
        CHECK(b != nullptr);
        if (b != nullptr) *b = Json("two");
        CHECK_EQ(j["b"].str(), std::string("two"));
        Json arr = Json::array();
        arr.set("x", 1);  // ignored: not an object
        CHECK(arr.isArray() && arr.empty());
        Json num(5);
        num.push(1);  // ignored: not an array
        CHECK(num.isNumber() && num.empty());
        CHECK_EQ(Json("text").asNumber(-1.0), -1.0);
        CHECK_EQ(Json(2.0).asString("d"), std::string("d"));
        CHECK(Json(3.5).asInt(9) == 3);
        CHECK_EQ(Json(1e30).asInt(9), 9);
    }

#ifdef LT_DATA_DIR
    TEST_SECTION("vertical slice item data");
    {
        const std::string path = std::string(LT_DATA_DIR) + "/items/vertical_slice_weapons.json";
        const auto r = lt::core::loadJsonFile(path);
        CHECK(r.ok());
        if (!r.ok()) std::printf("%s\n", r.describe(path).c_str());
        const Json& items = r.value;
        CHECK(items.isArray());
        CHECK_EQ(items.size(), 4u);
        const Json& fang = items[std::size_t{0}];
        CHECK_EQ(fang["id"].str(), std::string("weapon.aetherfang"));
        CHECK_EQ(fang["family"].str(), std::string("greatblade"));
        CHECK_EQ(fang["damage"]["physical"].asInt(), 42);
        CHECK_EQ(fang["damage"]["aether"].asInt(), 8);
        CHECK_EQ(fang.numberOr("weight", 0.0), 7.2);
        CHECK_EQ(fang.stringList("sockets").size(), 3u);
        CHECK_EQ(fang["timing"]["windup_ms"].asInt(), 500);
        for (const Json& item : items.items()) {
            CHECK(item["id"].isString());
            CHECK(item["damage"].isObject());
            CHECK(item["lore_key"].isString());
            CHECK(item["timing"].isObject());
        }
        // Oversized files are rejected before they are read.
        const auto big = lt::core::loadJsonFile(path, 128, 16);
        CHECK(!big.ok());
        CHECK(big.error.find("larger than") != std::string::npos);
    }
#endif

    TEST_SECTION("missing file");
    {
        const auto r = lt::core::loadJsonFile("definitely/not/here.json");
        CHECK(!r.ok());
        CHECK_EQ(r.line, 0u);
        CHECK(r.describe("here.json").rfind("here.json: ", 0) == 0);
    }

    return lt::test::finish("json");
}

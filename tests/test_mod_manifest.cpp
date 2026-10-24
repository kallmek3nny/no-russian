// Sections 14/19/25 TCore mod manifests: SHA-256 and HMAC-SHA256 vectors, and the plugin policy
// (code plugins off by default on Android, signed permission manifest pinned to the library hash).
#include <cstddef>
#include <string>
#include <string_view>

#include "TestHarness.h"
#include "tcore/ModManifest.h"
#include "tcore/Sha256.h"

using namespace lt::core;

namespace {

constexpr std::string_view kKey = "rdgaming-publisher-key";
constexpr std::string_view kLibraryBytes = "MZ pretend plugin image v1";
constexpr std::string_view kSwappedBytes = "MZ swapped plugin image";

// A desktop plugin mod exactly as its publisher ships it, signed with kKey.
ModManifest pluginMod() {
    ModManifest m;
    m.id = "frost_tweaks";
    m.titleKey = "mod.frost_tweaks.title";
    m.version = "1.0.0";
    m.contentSchema = 1;
    m.content = {ModContent::DataRecords, ModContent::Recipes};
    m.hasPlugin = true;
    m.plugin.library = "plugin.dll";
    m.plugin.librarySha256 = digestHex(sha256(kLibraryBytes));
    m.plugin.publisher = "rdgaming";
    m.plugin.permissions = {PluginPermission::ReadModData, PluginPermission::UiOverlay};
    m.plugin.signature = signPlugin(m, kKey);
    return m;
}

PluginPolicy desktopPolicy() {
    PluginPolicy p;
    p.platform = ModPlatform::Windows;
    p.keyring.push_back({"rdgaming", std::string(kKey)});
    return p;
}

} // namespace

int main() {
    TEST_SECTION("sha-256 and hmac-sha256 match the published vectors");
    CHECK_EQ(digestHex(sha256("")), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(digestHex(sha256("abc")), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(digestHex(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    CHECK_EQ(digestHex(sha256(std::string(1000000, 'a'))),
             std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    CHECK_EQ(digestHex(hmacSha256("Jefe", "what do ya want for nothing?")),
             std::string("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
    // RFC 4231 case 6: a key longer than the block size is hashed first.
    const std::string longKey(131, static_cast<char>(0xaa));
    CHECK_EQ(digestHex(hmacSha256(longKey, "Test Using Larger Than Block-Size Key - Hash Key First")),
             std::string("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));
    CHECK(digestEqual(sha256("abc"), sha256("abc")));
    CHECK(!digestEqual(sha256("abc"), sha256("abd")));

    TEST_SECTION("plugin policy: Android off by default, strangers, tampering and swapped binaries");
    {
        const PluginPolicy desktop = desktopPolicy();
        const ModManifest good = pluginMod();
        CHECK(evaluatePlugin(good, desktop, kLibraryBytes) == PluginVerdict::Allowed);

        PluginPolicy android = desktopPolicy();
        android.platform = ModPlatform::Android;
        CHECK(evaluatePlugin(good, android, kLibraryBytes) == PluginVerdict::DisabledOnPlatform);
        android.androidPluginsEnabled = true;
        CHECK(evaluatePlugin(good, android, kLibraryBytes) == PluginVerdict::Allowed);

        ModManifest unsignedMod = good;
        unsignedMod.plugin.signature.clear();
        CHECK(evaluatePlugin(unsignedMod, desktop, kLibraryBytes) == PluginVerdict::Unsigned);

        PluginPolicy stranger;
        stranger.keyring.push_back({"someone_else", std::string(kKey)});
        CHECK(evaluatePlugin(good, stranger, kLibraryBytes) == PluginVerdict::UnknownPublisher);

        ModManifest escalated = good; // a permission added after signing
        escalated.plugin.permissions.push_back(PluginPermission::Network);
        CHECK(evaluatePlugin(escalated, desktop, kLibraryBytes) == PluginVerdict::BadSignature);

        ModManifest reordered = good; // permission order is not part of the signature
        reordered.plugin.permissions = {PluginPermission::UiOverlay, PluginPermission::ReadModData};
        CHECK(evaluatePlugin(reordered, desktop, kLibraryBytes) == PluginVerdict::Allowed);

        ModManifest forged = good;
        forged.plugin.signature = signPlugin(good, "not-the-publisher-key");
        CHECK(evaluatePlugin(forged, desktop, kLibraryBytes) == PluginVerdict::BadSignature);
        forged.plugin.signature = "zz";
        CHECK(evaluatePlugin(forged, desktop, kLibraryBytes) == PluginVerdict::BadSignature);

        ModManifest repinned = good; // re-pinning the hash to a new binary breaks the signature
        repinned.plugin.librarySha256 = digestHex(sha256(kSwappedBytes));
        CHECK(evaluatePlugin(repinned, desktop, kSwappedBytes) == PluginVerdict::BadSignature);
        CHECK(evaluatePlugin(good, desktop, kSwappedBytes) == PluginVerdict::LibraryMismatch);
    }

    // mod.json fixtures: a data-only manifest plus single edits of it that each break one rule.
    // Raw strings stay out of CHECK() so MSVC's traditional preprocessor never stringizes them.
    const std::string dataMod =
        R"({"schema": 1, "id": "frost_tweaks", "title_key": "mod.frost_tweaks.title", )"
        R"("version": "1.0.0", "content_schema": 1, "content": ["data_records", "recipes"]})";
    const auto edit = [](std::string text, std::string_view from, std::string_view to) {
        const std::size_t at = text.find(from);
        if (at != std::string::npos) text.replace(at, from.size(), to);
        return text;
    };
    // True when text fails to load for the expected reason and the manifest is reset to defaults.
    const auto rejects = [](const std::string& text, std::string_view why) {
        const ModManifestResult r = parseModManifest(text);
        const bool matched = !r.ok() && r.error.find(why) != std::string::npos && r.manifest.id.empty();
        if (!matched)
            std::printf("  expected an error containing '%.*s', got '%s'\n", static_cast<int>(why.size()),
                        why.data(), r.error.c_str());
        return matched;
    };
    struct Broken {
        std::string_view from, to, why;
    };

    TEST_SECTION("mod.json: a data-only manifest loads and each broken rule is reported");
    {
        const ModManifestResult r = parseModManifest(dataMod, "frost_tweaks");
        CHECK(r.ok());
        if (!r.ok()) std::printf("  %s\n", r.error.c_str());
        CHECK_EQ(r.manifest.id, std::string("frost_tweaks"));
        CHECK_EQ(r.manifest.titleKey, std::string("mod.frost_tweaks.title"));
        CHECK_EQ(r.manifest.version, std::string("1.0.0"));
        CHECK_EQ(r.manifest.contentSchema, 1u);
        CHECK(r.manifest.content.size() == 2 && r.manifest.content[0] == ModContent::DataRecords &&
              r.manifest.content[1] == ModContent::Recipes);
        CHECK(!r.manifest.hasPlugin);
        CHECK(evaluatePlugin(r.manifest, desktopPolicy(), {}) == PluginVerdict::NoPlugin);
        CHECK(parseModManifest(dataMod).ok()); // without a folder the id is not cross-checked

        const ModManifestResult moved = parseModManifest(dataMod, "other_mod");
        CHECK(!moved.ok() && moved.manifest.id.empty());
        CHECK(moved.error.rfind("mods/other_mod/mod.json: ", 0) == 0);
        CHECK(moved.error.find("does not match its folder 'mods/other_mod/'") != std::string::npos);

        const Broken broken[] = {
            {R"("schema": 1)", R"("schema": 2)", "unsupported manifest schema 2"},
            {R"("schema": 1, )", R"("schema": 1, "author": "kenny", )", "unknown key 'author'"},
            {R"("schema": 1, )", R"("schema": 1, "id": "dupe", )", "duplicate"},
            {R"("frost_tweaks")", R"("../evil")", "invalid mod id '../evil'"},
            {R"("frost_tweaks")", R"("con")", "invalid mod id 'con'"},
            {R"("frost_tweaks")", "5", "'id' must be a string, not "},
            {R"("mod.frost_tweaks.title")", R"("Frost Tweaks")", "'title_key' is not a localisation key"},
            {R"("version": "1.0.0", )", "", "missing 'version'"},
            {R"("1.0.0")", R"("v1")", "invalid 'version' 'v1'"},
            {R"("content_schema": 1)", R"("content_schema": -1)", "'content_schema' must be a non-negative"},
            {R"("content_schema": 1)", R"("content_schema": 1.5)", "'content_schema' must be a non-negative"},
            {R"("recipes"])", R"("shaders"])", "'content' has unknown entry"},
            {R"("recipes"])", R"("data_records"])", "'content' lists 'data_records' twice"},
            {R"(["data_records", "recipes"])", "[]", "mod declares no content and no plugin"},
            {R"(["data_records", "recipes"])", "[[[[[[[[[[]]]]]]]]]]", "nesting"},
        };
        for (const Broken& b : broken) CHECK(rejects(edit(dataMod, b.from, b.to), b.why));
        CHECK(rejects("[1]", "manifest must be an object, not "));
        CHECK(rejects(std::string(static_cast<std::size_t>(kModManifestMaxBytes) + 1, ' '),
                      "file is larger than 65536 bytes"));

        const std::string truncated = R"({"schema": 1,)";
        CHECK(parseModManifest(truncated, "frost_tweaks").error.rfind("mods/frost_tweaks/mod.json:1:", 0) == 0);
    }

    TEST_SECTION("mod.json: a signed plugin block parses into a manifest the policy accepts");
    {
        const ModManifest shipped = pluginMod();
        const std::string sigField = R"(, "signature": ")" + shipped.plugin.signature + "\"";
        const std::string pluginJson =
            edit(dataMod, "]}", "], ") + R"("plugin": {"library": "plugin.dll", "library_sha256": ")" +
            shipped.plugin.librarySha256 +
            R"(", "publisher": "rdgaming", "permissions": ["ui_overlay", "read_mod_data"])" + sigField + "}}";

        const ModManifestResult r = parseModManifest(pluginJson, "frost_tweaks");
        CHECK(r.ok());
        if (!r.ok()) std::printf("  %s\n", r.error.c_str());
        CHECK(r.manifest.hasPlugin);
        CHECK_EQ(r.manifest.plugin.library, std::string("plugin.dll"));
        CHECK_EQ(r.manifest.plugin.publisher, std::string("rdgaming"));
        CHECK_EQ(r.manifest.plugin.signature, shipped.plugin.signature);
        CHECK(hasPermission(r.manifest.plugin, PluginPermission::UiOverlay) &&
              hasPermission(r.manifest.plugin, PluginPermission::ReadModData) &&
              !hasPermission(r.manifest.plugin, PluginPermission::Network));
        CHECK_EQ(signingPayload(r.manifest), signingPayload(shipped)); // listed order is not signed
        const PluginPolicy desktop = desktopPolicy();
        CHECK(evaluatePlugin(r.manifest, desktop, kLibraryBytes) == PluginVerdict::Allowed);
        CHECK(evaluatePlugin(r.manifest, desktop, kSwappedBytes) == PluginVerdict::LibraryMismatch);
        PluginPolicy android = desktop;
        android.platform = ModPlatform::Android;
        CHECK(evaluatePlugin(r.manifest, android, kLibraryBytes) == PluginVerdict::DisabledOnPlatform);

        // A missing signature is not a parse error: the data parts load and only the code is refused.
        const ModManifestResult unsignedR = parseModManifest(edit(pluginJson, sigField, ""), "frost_tweaks");
        CHECK(unsignedR.ok() && unsignedR.manifest.hasPlugin && unsignedR.manifest.plugin.signature.empty());
        CHECK(evaluatePlugin(unsignedR.manifest, desktop, kLibraryBytes) == PluginVerdict::Unsigned);

        // A code-only mod may declare no data content; content is not part of the signature.
        const std::string codeOnlyJson = edit(pluginJson, R"(["data_records", "recipes"])", "[]");
        const ModManifestResult codeOnly = parseModManifest(codeOnlyJson);
        CHECK(codeOnly.ok() && codeOnly.manifest.hasPlugin && codeOnly.manifest.content.empty());
        CHECK(evaluatePlugin(codeOnly.manifest, desktop, kLibraryBytes) == PluginVerdict::Allowed);

        const std::string notObject = edit(dataMod, "]}", R"(], "plugin": [1]})");
        CHECK(rejects(notObject, "'plugin' must be an object, not "));
        const Broken brokenPlugin[] = {
            {R"("plugin.dll")", R"("../plugin.dll")", "'plugin.library' must name a file directly inside"},
            {R"("plugin.dll")", R"("COM1.dll")", "'plugin.library' must name a file directly inside"},
            {R"("plugin.dll")", "7", "'plugin.library' must be a string, not "},
            {R"("publisher": "rdgaming", )", "", "missing 'plugin.publisher'"},
            {R"("rdgaming")", R"("")", "'plugin.publisher' must be 1-48 characters"},
            {R"("publisher": "rdgaming", )", R"("publisher": "rdgaming", "autorun": true, )",
             "unknown key 'plugin.autorun'"},
            {R"("ui_overlay", )", R"("root_shell", )", "'plugin.permissions' has unknown entry"},
            {R"("ui_overlay", )", R"("read_mod_data", )", "'plugin.permissions' lists 'read_mod_data' twice"},
            {R"(["ui_overlay", "read_mod_data"])", R"("ui_overlay")",
             "'plugin.permissions' must be an array, not "},
            {sigField, R"(, "signature": 7)", "'plugin.signature' must be a string, not "},
        };
        for (const Broken& b : brokenPlugin) CHECK(rejects(edit(pluginJson, b.from, b.to), b.why));
    }

    TEST_SECTION("loadModManifest: reads mods/<id>/mod.json from disk and checks the folder name");
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path root = fs::temp_directory_path(ec) / "lt_test_mod_manifest";
        fs::remove_all(root, ec);
        const auto writeFile = [](const fs::path& p, std::string_view text) {
            std::error_code dirEc;
            fs::create_directories(p.parent_path(), dirEc);
            std::ofstream out(p, std::ios::binary | std::ios::trunc);
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            return static_cast<bool>(out);
        };
        const fs::path mods = root / "mods";

        CHECK(writeFile(mods / "frost_tweaks" / "mod.json", dataMod));
        const ModManifestResult r = loadModManifest(mods / "frost_tweaks");
        CHECK(r.ok());
        if (!r.ok()) std::printf("  %s\n", r.error.c_str());
        CHECK_EQ(r.manifest.id, std::string("frost_tweaks"));
        CHECK(r.manifest.content.size() == 2 && !r.manifest.hasPlugin);
        const ModManifestResult trailing = loadModManifest(mods / "frost_tweaks" / ""); // "mods/frost_tweaks/"
        CHECK(trailing.ok() && trailing.manifest.id == "frost_tweaks");

        // The same file copied into another folder no longer matches its id.
        CHECK(writeFile(mods / "other_mod" / "mod.json", dataMod));
        const ModManifestResult moved = loadModManifest(mods / "other_mod");
        CHECK(!moved.ok() && moved.manifest.id.empty());
        CHECK(moved.error.rfind("mods/other_mod/mod.json: ", 0) == 0);
        CHECK(moved.error.find("does not match its folder 'mods/other_mod/'") != std::string::npos);

        // Folder names are checked before anything is read, including non-ASCII names.
        const ModManifestResult badFolder = loadModManifest(mods / "Bad Folder");
        CHECK(badFolder.error.find("mod folder 'mods/Bad Folder/' is not a valid mod id") != std::string::npos);
        const fs::path unicodeFolder = mods / fs::path(u8"fröst");
        const ModManifestResult nonAscii = loadModManifest(unicodeFolder);
        CHECK(nonAscii.error.rfind("mod folder name is not a valid mod id", 0) == 0);

        const ModManifestResult missing = loadModManifest(mods / "empty_mod");
        CHECK(!missing.ok() && missing.error.rfind("mods/empty_mod/mod.json: cannot open file", 0) == 0);

        const std::string oversized(static_cast<std::size_t>(kModManifestMaxBytes) + 1, ' ');
        CHECK(writeFile(mods / "big_mod" / "mod.json", oversized));
        const ModManifestResult big = loadModManifest(mods / "big_mod");
        CHECK(big.error == "mods/big_mod/mod.json: file is larger than 65536 bytes");

        CHECK(writeFile(mods / "broken_mod" / "mod.json", "{\"schema\": 1,\r\n"));
        const ModManifestResult broken = loadModManifest(mods / "broken_mod");
        // CRLF counts as one line break, so the end of input sits at line 2, column 1.
        const bool brokenAt = broken.error.rfind("mods/broken_mod/mod.json:2:1: unexpected end of input", 0) == 0;
        CHECK(brokenAt && broken.manifest.id.empty());
        if (!brokenAt) std::printf("  got '%s'\n", broken.error.c_str());

        fs::remove_all(root, ec);
    }

    return lt::test::finish("mod_manifest");
}

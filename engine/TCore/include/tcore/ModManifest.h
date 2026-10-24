#pragma once
// TCore mod manifests (Sections 14, 19, 25). Mods live in mods/<id>/ and may add data
// records, palettes, recipes, enemy variants, dungeons, dialogue and event templates.
// Code plugins are disabled by default on Android and need a signed permission manifest
// on desktop: an HMAC-SHA256 signature, keyed by the publisher, over a canonical payload
// that also pins the SHA-256 of the plugin library so a swapped binary is rejected.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "tcore/Json.h"
#include "tcore/JsonParser.h"
#include "tcore/Sha256.h"

namespace lt::core {

inline constexpr std::uint32_t kModManifestSchema = 1;
inline constexpr std::size_t kMaxModIdLength = 48;

enum class ModContent : std::uint8_t {
    DataRecords = 0, Palettes, Recipes, EnemyVariants, Dungeons, Dialogue, EventTemplates, Count
};
inline constexpr std::array<std::string_view, static_cast<std::size_t>(ModContent::Count)> kModContentNames = {
    "data_records", "palettes", "recipes", "enemy_variants", "dungeons", "dialogue", "event_templates"};

// Capabilities a desktop code plugin may request; each must be listed in the signed manifest.
enum class PluginPermission : std::uint8_t {
    ReadModData = 0, WriteModSaves, SimulationHooks, UiOverlay, AudioPlayback, Network, Count
};
inline constexpr std::array<std::string_view, static_cast<std::size_t>(PluginPermission::Count)>
    kPluginPermissionNames = {"read_mod_data", "write_mod_saves", "simulation_hooks",
                              "ui_overlay",    "audio_playback",  "network"};

template <typename E, std::size_t N>
bool enumFromName(const std::array<std::string_view, N>& names, std::string_view s, E& out) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        if (names[i] == s) {
            out = static_cast<E>(i);
            return true;
        }
    }
    return false;
}

inline std::string_view modContentName(ModContent c) noexcept {
    const auto i = static_cast<std::size_t>(c);
    return i < kModContentNames.size() ? kModContentNames[i] : std::string_view("unknown");
}
inline std::string_view permissionName(PluginPermission p) noexcept {
    const auto i = static_cast<std::size_t>(p);
    return i < kPluginPermissionNames.size() ? kPluginPermissionNames[i] : std::string_view("unknown");
}

// A mod id names its folder under mods/, so it is limited to [a-z0-9_-], starts with a
// letter, and can never hold separators, dots, drive letters or Windows device names.
inline bool isValidModId(std::string_view id) noexcept {
    if (id.empty() || id.size() > kMaxModIdLength || id.front() < 'a' || id.front() > 'z') return false;
    for (const char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    if (id == "con" || id == "prn" || id == "aux" || id == "nul") return false;
    const bool numbered = id.size() == 4 && (id.substr(0, 3) == "com" || id.substr(0, 3) == "lpt");
    return !(numbered && id[3] >= '1' && id[3] <= '9');
}

struct ModPlugin {
    std::string library;                       // file inside mods/<id>/, e.g. "plugin.dll"
    std::string librarySha256;                 // lowercase hex digest of that file
    std::string publisher;                     // keyring entry whose key signed the manifest
    std::vector<PluginPermission> permissions; // requested capabilities
    std::string signature;                     // hex HMAC-SHA256 of signingPayload()
};

struct ModManifest {
    std::uint32_t schema = kModManifestSchema;
    std::string id;
    std::string titleKey;           // localisation key shown in the Mod Data menu (Section 26)
    std::string version;            // the mod's own release, e.g. "1.2.0"
    std::uint32_t contentSchema = 0; // data schema its records target (save compatibility)
    std::vector<ModContent> content;
    bool hasPlugin = false;
    ModPlugin plugin;
};

inline bool hasPermission(const ModPlugin& p, PluginPermission perm) noexcept {
    return std::find(p.permissions.begin(), p.permissions.end(), perm) != p.permissions.end();
}

// Canonical bytes covered by the plugin signature. Each field is "key:length:value\n" so no
// value can forge a field boundary, and permissions are sorted so their order is irrelevant.
inline std::string signingPayload(const ModManifest& m) {
    std::vector<std::string_view> perms;
    perms.reserve(m.plugin.permissions.size());
    for (const PluginPermission p : m.plugin.permissions) perms.push_back(permissionName(p));
    std::sort(perms.begin(), perms.end());
    perms.erase(std::unique(perms.begin(), perms.end()), perms.end());
    std::string out = "lt-mod-plugin-v1\n";
    const auto field = [&out](std::string_view key, std::string_view value) {
        out += key;
        out += ':';
        out += std::to_string(value.size());
        out += ':';
        out += value;
        out += '\n';
    };
    field("schema", std::to_string(m.schema));
    field("id", m.id);
    field("version", m.version);
    field("content_schema", std::to_string(m.contentSchema));
    field("library", m.plugin.library);
    field("library_sha256", m.plugin.librarySha256);
    field("publisher", m.plugin.publisher);
    for (const std::string_view p : perms) field("permission", p);
    return out;
}

// Publisher-side helper (and test fixture): the hex signature to place in the manifest.
inline std::string signPlugin(const ModManifest& m, std::string_view publisherKey) {
    return digestHex(hmacSha256(publisherKey, signingPayload(m)));
}

// Strict 64-character lowercase hex, matching digestHex output.
inline bool parseDigestHex(std::string_view hex, Sha256Digest& out) noexcept {
    if (hex.size() != 64) return false;
    const auto nibble = [](char c) noexcept -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int hi = nibble(hex[2 * i]), lo = nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return true;
}

enum class ModPlatform : std::uint8_t { Windows = 0, Linux, Android };

struct PublisherKey {
    std::string publisher;
    std::string key; // HMAC secret shipped with the install's trusted publisher list
};

// Per-install plugin policy. Data-only mods never consult it.
struct PluginPolicy {
    ModPlatform platform = ModPlatform::Windows;
    bool androidPluginsEnabled = false; // Section 25: code plugins are off by default on Android
    std::vector<PublisherKey> keyring;

    const PublisherKey* find(std::string_view publisher) const noexcept {
        for (const PublisherKey& k : keyring)
            if (k.publisher == publisher) return &k;
        return nullptr;
    }
};

enum class PluginVerdict : std::uint8_t {
    NoPlugin = 0, Allowed, DisabledOnPlatform, Unsigned, UnknownPublisher, BadSignature, LibraryMismatch
};

inline std::string_view pluginVerdictName(PluginVerdict v) noexcept {
    switch (v) {
    case PluginVerdict::NoPlugin: return "no_plugin";
    case PluginVerdict::Allowed: return "allowed";
    case PluginVerdict::DisabledOnPlatform: return "disabled_on_platform";
    case PluginVerdict::Unsigned: return "unsigned";
    case PluginVerdict::UnknownPublisher: return "unknown_publisher";
    case PluginVerdict::BadSignature: return "bad_signature";
    case PluginVerdict::LibraryMismatch: return "library_mismatch";
    }
    return "unknown";
}

// Decides whether a mod's code plugin may load. libraryBytes is the plugin file as read from
// mods/<id>/; the loader must load exactly these bytes so the file cannot be swapped after the check.
inline PluginVerdict evaluatePlugin(const ModManifest& m, const PluginPolicy& policy,
                                    std::string_view libraryBytes) {
    if (!m.hasPlugin) return PluginVerdict::NoPlugin;
    if (policy.platform == ModPlatform::Android && !policy.androidPluginsEnabled)
        return PluginVerdict::DisabledOnPlatform;
    if (m.plugin.signature.empty()) return PluginVerdict::Unsigned;
    const PublisherKey* key = policy.find(m.plugin.publisher);
    if (key == nullptr) return PluginVerdict::UnknownPublisher;
    Sha256Digest claimed{};
    if (!parseDigestHex(m.plugin.signature, claimed) ||
        !digestEqual(claimed, hmacSha256(key->key, signingPayload(m))))
        return PluginVerdict::BadSignature;
    Sha256Digest pinned{};
    if (!parseDigestHex(m.plugin.librarySha256, pinned) || !digestEqual(pinned, sha256(libraryBytes)))
        return PluginVerdict::LibraryMismatch;
    return PluginVerdict::Allowed;
}

// ---- mods/<id>/mod.json -------------------------------------------------------------------
//   { "schema": 1, "id": "frost_tweaks", "title_key": "mod.frost_tweaks.title",
//     "version": "1.0.0", "content_schema": 1, "content": ["data_records", "recipes"],
//     "plugin": { "library": "plugin.dll", "library_sha256": "<64 hex>", "publisher": "rdgaming",
//                 "permissions": ["read_mod_data"], "signature": "<64 hex>" } }
// Unknown keys are rejected so a typo cannot silently drop a field. A missing or malformed
// signature is not a parse error: the data parts still load and evaluatePlugin denies the code.
inline constexpr std::string_view kModManifestFile = "mod.json";

// "1.2.0", "0.3.1-beta+5": starts with a digit, then [0-9A-Za-z.+-], at most 32 characters.
inline bool isValidModVersion(std::string_view v) noexcept {
    if (v.empty() || v.size() > 32 || v.front() < '0' || v.front() > '9') return false;
    for (const char c : v) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '.' || c == '-' || c == '+';
        if (!ok) return false;
    }
    return true;
}

// A plugin library must sit directly inside mods/<id>/: no separators, drive letters, leading
// or trailing dots (so no ".."), and no Windows device stems such as "con.dll".
inline bool isPlainModFileName(std::string_view name) {
    if (name.empty() || name.size() > 128 || name.front() == '.' || name.back() == '.') return false;
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
    std::string stem(name.substr(0, name.find('.')));
    for (char& c : stem)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") return false;
    const bool numbered = stem.size() == 4 && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0);
    return !(numbered && stem[3] >= '1' && stem[3] <= '9');
}

struct ModManifestResult {
    ModManifest manifest;
    std::string error; // empty on success
    bool ok() const noexcept { return error.empty(); }
    explicit operator bool() const noexcept { return ok(); }
};

namespace detail {

template <std::size_t N>
bool onlyKeys(const Json& obj, const std::array<std::string_view, N>& allowed, std::string_view where,
              std::string& err) {
    for (const std::string& k : obj.keys()) {
        if (std::find(allowed.begin(), allowed.end(), std::string_view(k)) == allowed.end()) {
            err = "unknown key '" + std::string(where) + k + "'";
            return false;
        }
    }
    return true;
}

inline bool readString(const Json& obj, std::string_view key, std::string_view where, bool required,
                       std::string& out, std::string& err) {
    const Json* v = obj.find(key);
    if (v == nullptr) {
        if (required) err = "missing '" + std::string(where) + std::string(key) + "'";
        return !required;
    }
    if (!v->isString()) {
        err = "'" + std::string(where) + std::string(key) + "' must be a string, not " + v->typeName();
        return false;
    }
    out = v->str();
    return true;
}

inline bool readUint32(const Json& obj, std::string_view key, std::uint32_t& out, std::string& err) {
    const Json* v = obj.find(key);
    if (v == nullptr) {
        err = "missing '" + std::string(key) + "'";
        return false;
    }
    const double d = v->asNumber(-1.0);
    if (!v->isInteger() || d < 0.0 || d > 4294967295.0) {
        err = "'" + std::string(key) + "' must be a non-negative 32-bit integer";
        return false;
    }
    out = static_cast<std::uint32_t>(d);
    return true;
}

// Same rule as loc::StringTable::validKey: lowercase "namespace.name" keys (Section 26).
inline bool isLocKey(std::string_view k) noexcept {
    if (k.empty() || k.front() == '.' || k.back() == '.') return false;
    bool dot = false;
    for (const char c : k) {
        if (c == '.') {
            dot = true;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return dot;
}

// Array of enum names; unknown or repeated entries are errors.
template <typename E, std::size_t N>
bool readEnumList(const Json& obj, std::string_view key, std::string_view where,
                  const std::array<std::string_view, N>& names, std::vector<E>& out, std::string& err) {
    const std::string label = std::string(where) + std::string(key);
    const Json* v = obj.find(key);
    if (v == nullptr) {
        err = "missing '" + label + "'";
        return false;
    }
    if (!v->isArray()) {
        err = "'" + label + "' must be an array, not " + v->typeName();
        return false;
    }
    for (const Json& item : v->items()) {
        E e{};
        if (!item.isString() || !enumFromName(names, item.str(), e)) {
            err = "'" + label + "' has unknown entry " + item.dump();
            return false;
        }
        if (std::find(out.begin(), out.end(), e) != out.end()) {
            err = "'" + label + "' lists '" + item.str() + "' twice";
            return false;
        }
        out.push_back(e);
    }
    return true;
}

inline bool manifestError(std::string& err, std::string message) {
    err.swap(message);
    return false;
}

inline bool readPlugin(const Json& v, ModPlugin& p, std::string& err) {
    static constexpr std::array<std::string_view, 5> kKeys = {"library", "library_sha256", "publisher",
                                                              "permissions", "signature"};
    if (!v.isObject())
        return manifestError(err, std::string("'plugin' must be an object, not ") + v.typeName());
    if (!onlyKeys(v, kKeys, "plugin.", err) ||
        !readString(v, "library", "plugin.", true, p.library, err) ||
        !readString(v, "library_sha256", "plugin.", true, p.librarySha256, err) ||
        !readString(v, "publisher", "plugin.", true, p.publisher, err) ||
        !readString(v, "signature", "plugin.", false, p.signature, err))
        return false;
    if (!isPlainModFileName(p.library))
        return manifestError(err, "'plugin.library' must name a file directly inside the mod folder, not '" +
                                      p.library + "'");
    if (p.publisher.empty() || p.publisher.size() > kMaxModIdLength)
        return manifestError(err, "'plugin.publisher' must be 1-" + std::to_string(kMaxModIdLength) +
                                      " characters");
    return readEnumList(v, "permissions", "plugin.", kPluginPermissionNames, p.permissions, err);
}

// Fills m from a decoded mod.json; folder (when non-empty) is the mods/<id>/ folder it came from.
inline bool readManifest(const Json& doc, std::string_view folder, ModManifest& m, std::string& err) {
    static constexpr std::array<std::string_view, 7> kKeys = {
        "schema", "id", "title_key", "version", "content_schema", "content", "plugin"};
    if (!doc.isObject())
        return manifestError(err, std::string("manifest must be an object, not ") + doc.typeName());
    if (!onlyKeys(doc, kKeys, "", err) || !readUint32(doc, "schema", m.schema, err)) return false;
    if (m.schema != kModManifestSchema)
        return manifestError(err, "unsupported manifest schema " + std::to_string(m.schema) +
                                      " (this build reads " + std::to_string(kModManifestSchema) + ")");
    if (!readString(doc, "id", "", true, m.id, err)) return false;
    if (!isValidModId(m.id)) return manifestError(err, "invalid mod id '" + m.id + "': use [a-z][a-z0-9_-]*");
    if (!folder.empty() && folder != m.id)
        return manifestError(err, "mod id '" + m.id + "' does not match its folder 'mods/" +
                                      std::string(folder) + "/'");
    if (!readString(doc, "title_key", "", true, m.titleKey, err)) return false;
    if (!isLocKey(m.titleKey))
        return manifestError(err, "'title_key' is not a localisation key: '" + m.titleKey + "'");
    if (!readString(doc, "version", "", true, m.version, err)) return false;
    if (!isValidModVersion(m.version)) return manifestError(err, "invalid 'version' '" + m.version + "'");
    if (!readUint32(doc, "content_schema", m.contentSchema, err) ||
        !readEnumList(doc, "content", "", kModContentNames, m.content, err))
        return false;
    const Json* plugin = doc.find("plugin");
    if (plugin == nullptr) {
        if (m.content.empty()) return manifestError(err, "mod declares no content and no plugin");
        return true;
    }
    if (!readPlugin(*plugin, m.plugin, err)) return false;
    m.hasPlugin = true;
    return true;
}

// Turns a parsed mod.json into a result; source labels errors, e.g. "mods/frost_tweaks/mod.json".
inline ModManifestResult manifestFromParse(const JsonParseResult& parsed, std::string_view folder,
                                           std::string_view source) {
    ModManifestResult result;
    if (!parsed) {
        result.error = parsed.describe(source);
        return result;
    }
    std::string err;
    if (!readManifest(parsed.value, folder, result.manifest, err)) {
        result.manifest = ModManifest{};
        result.error = std::string(source) + ": " + err;
    }
    return result;
}

inline std::string manifestSource(std::string_view folder) {
    if (folder.empty()) return std::string(kModManifestFile);
    return "mods/" + std::string(folder) + "/" + std::string(kModManifestFile);
}

} // namespace detail

// A manifest is a handful of fields; anything deeper or larger is malformed or hostile.
inline constexpr int kModManifestMaxDepth = 8;
inline constexpr std::uintmax_t kModManifestMaxBytes = 64 * 1024;

// Parses mod.json text. folder, when non-empty, is the mods/<id>/ folder name and must equal the id.
// On failure the manifest is reset to defaults and error says where and why.
inline ModManifestResult parseModManifest(std::string_view text, std::string_view folder = {}) {
    const std::string source = detail::manifestSource(folder);
    if (text.size() > kModManifestMaxBytes) {
        ModManifestResult result;
        result.error = source + ": file is larger than " + std::to_string(kModManifestMaxBytes) + " bytes";
        return result;
    }
    return detail::manifestFromParse(parseJson(text, kModManifestMaxDepth), folder, source);
}

// Loads <modDir>/mod.json, where modDir is a mods/<id>/ folder (a trailing separator is allowed).
// The folder name must itself be a valid mod id; it is read from the native path one ASCII unit at
// a time, so a non-ASCII folder name is rejected rather than converted (which can throw on Windows).
inline ModManifestResult loadModManifest(const std::filesystem::path& modDir) {
    const std::filesystem::path dir = modDir.has_filename() ? modDir : modDir.parent_path();
    std::string folder;
    bool ascii = true;
    // Keep the filename alive: before C++23 a range-for does not extend the temporary path behind
    // native(), so iterating dir.filename().native() directly walks a destroyed string.
    const std::filesystem::path name = dir.filename();
    for (const auto c : name.native()) {
        const auto u = static_cast<std::uint32_t>(c);
        if (u == 0 || u > 0x7f) {
            ascii = false;
            break;
        }
        folder.push_back(static_cast<char>(u));
    }
    if (!ascii || !isValidModId(folder)) {
        ModManifestResult result;
        result.error = ascii ? "mod folder 'mods/" + folder + "/' is not a valid mod id: use [a-z][a-z0-9_-]*"
                             : std::string("mod folder name is not a valid mod id: use [a-z][a-z0-9_-]*");
        return result;
    }
    const JsonParseResult parsed =
        loadJsonFile(dir / std::filesystem::path(kModManifestFile), kModManifestMaxDepth, kModManifestMaxBytes);
    return detail::manifestFromParse(parsed, folder, detail::manifestSource(folder));
}

} // namespace lt::core

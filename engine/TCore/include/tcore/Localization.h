#pragma once
// TCore localisation (Section 26): every player-facing line uses a stable key.
// English is the source language; Russian is the first target. Tables load from
// plain UTF-8 "key=value" text so no text is embedded in C++ or prefab binaries.
// Includes validation for missing keys, invalid UTF-8, glyph coverage (Cyrillic
// fallback) and the short mobile combat-prompt rule.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace lt::loc {

inline constexpr const char* kSourceLocale = "en";
inline constexpr const char* kFirstTargetLocale = "ru";
// Combat prompts stay short on mobile (GUARD, DEFLECT, SCAN, INFUSE, REST, BUILD).
inline constexpr std::size_t kMaxPromptCodepoints = 12;

// Decodes UTF-8 into codepoints. Returns nullopt on malformed input.
inline std::optional<std::vector<std::uint32_t>> decodeUtf8(const std::string& s) {
    std::vector<std::uint32_t> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = 0;
        int extra = 0;
        if (c < 0x80) { cp = c; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else return std::nullopt;
        if (i + static_cast<std::size_t>(extra) >= s.size() + (extra == 0 ? 1 : 0) && extra > 0 &&
            i + static_cast<std::size_t>(extra) > s.size() - 1) return std::nullopt;
        for (int k = 1; k <= extra; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
            if ((cc & 0xC0) != 0x80) return std::nullopt;
            cp = (cp << 6) | (cc & 0x3F);
        }
        // Reject overlong encodings and surrogates.
        if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) ||
            cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return std::nullopt;
        out.push_back(cp);
        i += static_cast<std::size_t>(extra) + 1;
    }
    return out;
}

// Glyph ranges a platform font stack covers (primary + fallback).
struct GlyphCoverage {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
    bool covers(std::uint32_t cp) const {
        for (const auto& r : ranges) if (cp >= r.first && cp <= r.second) return true;
        return false;
    }
    // Required on every platform: Basic Latin, Latin-1, General Punctuation, Cyrillic.
    static GlyphCoverage required() {
        return GlyphCoverage{{{0x0020, 0x007E}, {0x00A0, 0x00FF}, {0x2010, 0x205E}, {0x0400, 0x04FF}}};
    }
};

class StringTable {
public:
    explicit StringTable(std::string locale = kSourceLocale) : locale_(std::move(locale)) {}

    const std::string& locale() const noexcept { return locale_; }

    // Parses "key=value" lines; '#' comments and blank lines ignored; \n in values
    // becomes a newline. Returns false on a malformed line or a duplicate key.
    bool parse(const std::string& text, std::string* error = nullptr) {
        std::istringstream in(text);
        std::string line;
        int lineNo = 0;
        std::map<std::string, std::string> parsed;
        while (std::getline(in, line)) {
            ++lineNo;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos || eq == 0) return fail(error, lineNo, "expected key=value");
            std::string key = line.substr(0, eq);
            if (!validKey(key)) return fail(error, lineNo, "invalid key '" + key + "'");
            if (parsed.count(key)) return fail(error, lineNo, "duplicate key '" + key + "'");
            std::string value = unescape(line.substr(eq + 1));
            if (!decodeUtf8(value)) return fail(error, lineNo, "invalid UTF-8 in '" + key + "'");
            parsed.emplace(std::move(key), std::move(value));
        }
        for (auto& [k, v] : parsed) entries_[k] = std::move(v);
        return true;
    }

    void set(const std::string& key, std::string value) { entries_[key] = std::move(value); }
    bool has(const std::string& key) const { return entries_.count(key) != 0; }
    std::size_t size() const noexcept { return entries_.size(); }
    const std::map<std::string, std::string>& entries() const noexcept { return entries_; }

    const std::string* find(const std::string& key) const {
        auto it = entries_.find(key);
        return it == entries_.end() ? nullptr : &it->second;
    }

    // Stable keys: lowercase ascii, digits, '.', '_' and at least one '.' (namespace.name).
    static bool validKey(const std::string& k) {
        if (k.empty() || k.front() == '.' || k.back() == '.') return false;
        bool dot = false;
        for (char c : k) {
            if (c == '.') { dot = true; continue; }
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
        }
        return dot;
    }

private:
    static bool fail(std::string* error, int line, const std::string& msg) {
        if (error) *error = "line " + std::to_string(line) + ": " + msg;
        return false;
    }
    static std::string unescape(const std::string& s) {
        std::string out;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                const char n = s[i + 1];
                if (n == 'n') { out += '\n'; ++i; continue; }
                if (n == '\\') { out += '\\'; ++i; continue; }
            }
            out += s[i];
        }
        return out;
    }

    std::string locale_;
    std::map<std::string, std::string> entries_;
};

// Active locale with fallback to the English source table; missing keys render
// as the key itself (never blank) and are recorded for the content validator.
class Localizer {
public:
    explicit Localizer(StringTable source) : source_(std::move(source)) {}

    void setActive(StringTable t) { active_ = std::move(t); hasActive_ = true; }
    void clearActive() { hasActive_ = false; }
    const std::string& activeLocale() const { return hasActive_ ? active_.locale() : source_.locale(); }

    std::string get(const std::string& key) const {
        if (hasActive_) if (const auto* v = active_.find(key)) return *v;
        if (const auto* v = source_.find(key)) return *v;
        missing_.push_back(key);
        return key;
    }

    // Replaces {0}, {1}, ... with args.
    std::string format(const std::string& key, const std::vector<std::string>& args) const {
        std::string s = get(key);
        for (std::size_t i = 0; i < args.size(); ++i) {
            const std::string tok = "{" + std::to_string(i) + "}";
            for (auto p = s.find(tok); p != std::string::npos; p = s.find(tok, p + args[i].size()))
                s.replace(p, tok.size(), args[i]);
        }
        return s;
    }

    const std::vector<std::string>& missingKeys() const noexcept { return missing_; }

private:
    StringTable source_;
    StringTable active_;
    bool hasActive_ = false;
    mutable std::vector<std::string> missing_;
};

enum class LocIssueKind : std::uint8_t { MissingInTarget, ExtraInTarget, UncoveredGlyph, PromptTooLong, PlaceholderMismatch };

struct LocIssue {
    LocIssueKind kind;
    std::string key;
    std::string detail;
};

inline std::vector<std::string> placeholders(const std::string& s) {
    std::vector<std::string> out;
    for (std::size_t p = s.find('{'); p != std::string::npos; p = s.find('{', p + 1)) {
        const auto e = s.find('}', p);
        if (e == std::string::npos) break;
        out.push_back(s.substr(p, e - p + 1));
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Validator used by CI before content merges (Sections 25/26).
inline std::vector<LocIssue> validateTranslation(const StringTable& source, const StringTable& target,
                                                 const GlyphCoverage& fonts = GlyphCoverage::required()) {
    std::vector<LocIssue> issues;
    for (const auto& [k, v] : source.entries()) {
        const auto* t = target.find(k);
        if (!t) { issues.push_back({LocIssueKind::MissingInTarget, k, target.locale()}); continue; }
        if (placeholders(v) != placeholders(*t)) issues.push_back({LocIssueKind::PlaceholderMismatch, k, *t});
    }
    for (const auto& [k, v] : target.entries()) {
        if (!source.has(k)) issues.push_back({LocIssueKind::ExtraInTarget, k, target.locale()});
        const auto cps = decodeUtf8(v);
        if (cps) {
            for (auto cp : *cps) {
                if (cp == '\n' || fonts.covers(cp)) continue;
                issues.push_back({LocIssueKind::UncoveredGlyph, k, "U+" + std::to_string(cp)});
                break;
            }
            if (k.rfind("prompt.", 0) == 0 && cps->size() > kMaxPromptCodepoints)
                issues.push_back({LocIssueKind::PromptTooLong, k, std::to_string(cps->size())});
        }
    }
    return issues;
}

} // namespace lt::loc

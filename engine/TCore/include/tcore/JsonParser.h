#pragma once
// TCore JSON parser (Sections 21, 25, 26): strict RFC 8259 reader for the data records
// under data/ and mods/<id>/. Errors carry a 1-based line and column so the content
// validator can point a modder at the exact spot.
// - Input must be valid UTF-8; a leading UTF-8 byte-order mark is skipped.
// - No comments, trailing commas, NaN/Infinity, leading zeros, or raw control characters.
// - Duplicate object keys are rejected: an ambiguous record is a content bug.
// - Nesting depth is bounded so a hostile mod file cannot exhaust the stack.
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <version>

#include "tcore/Json.h"

namespace lt::core {

struct JsonParseResult {
    Json value;
    std::string error;       // empty on success
    std::size_t offset = 0;  // byte offset of the error in the input
    std::size_t line = 0;    // 1-based; 0 when the error has no position (e.g. missing file)
    std::size_t column = 0;  // 1-based, counted in UTF-8 code points

    bool ok() const noexcept { return error.empty(); }
    explicit operator bool() const noexcept { return ok(); }

    // "source:line:column: message" (position omitted when unknown); empty on success.
    std::string describe(std::string_view source = "json") const {
        if (ok()) return {};
        std::string out(source);
        if (line != 0) {
            out += ':';
            out += std::to_string(line);
            out += ':';
            out += std::to_string(column);
        }
        out += ": ";
        out += error;
        return out;
    }
};

namespace detail {

class JsonParser {
public:
    static constexpr int kDefaultMaxDepth = 128;

    JsonParser(std::string_view text, int maxDepth) noexcept : text_(text), maxDepth_(maxDepth) {}

    JsonParseResult run() {
        if (text_.size() >= 3 && byte(0) == 0xEF && byte(1) == 0xBB && byte(2) == 0xBF) {
            pos_ = 3;
            start_ = 3;
        }
        skipWhitespace();
        Json value;
        if (parseValue(value, 0)) {
            skipWhitespace();
            if (more()) fail("unexpected " + describeHere() + " after the top-level value");
        }
        JsonParseResult result;
        if (!error_.empty()) {
            result.error = std::move(error_);
            result.offset = errorPos_;
            locate(errorPos_, result.line, result.column);
            return result;
        }
        result.value = std::move(value);
        return result;
    }

private:
    std::uint8_t byte(std::size_t where) const noexcept {
        return static_cast<std::uint8_t>(text_[where]);
    }
    bool more() const noexcept { return pos_ < text_.size(); }
    bool at(char c) const noexcept { return pos_ < text_.size() && text_[pos_] == c; }
    static bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }
    bool digitHere() const noexcept { return pos_ < text_.size() && isDigit(text_[pos_]); }

    bool fail(std::string message) { return failAt(pos_, std::move(message)); }
    bool failAt(std::size_t where, std::string message) {
        if (error_.empty()) {
            error_ = std::move(message);
            errorPos_ = where;
        }
        return false;
    }

    void locate(std::size_t where, std::size_t& line, std::size_t& column) const noexcept {
        line = 1;
        column = 1;
        const std::size_t end = where < text_.size() ? where : text_.size();
        for (std::size_t i = start_; i < end; ++i) {
            const std::uint8_t c = byte(i);
            if (c == '\n') {
                ++line;
                column = 1;
            } else if ((c & 0xC0) != 0x80) {
                ++column;  // continuation bytes belong to the previous code point
            }
        }
    }

    std::string describeHere() const {
        if (!more()) return "end of input";
        const std::uint8_t c = byte(pos_);
        if (c > 0x20 && c < 0x7F) return std::string("'") + static_cast<char>(c) + "'";
        static constexpr char kHex[] = "0123456789abcdef";
        std::string s = "byte 0x";
        s += kHex[c >> 4];
        s += kHex[c & 0x0F];
        return s;
    }

    void skipWhitespace() noexcept {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return;
            ++pos_;
        }
    }

    bool checkDepth(int depth) {
        if (depth <= maxDepth_) return true;
        return fail("nesting deeper than " + std::to_string(maxDepth_) + " levels");
    }

    bool parseValue(Json& out, int depth) {
        if (!more()) return fail("unexpected end of input, expected a value");
        const char c = text_[pos_];
        switch (c) {
            case '{': return parseObject(out, depth + 1);
            case '[': return parseArray(out, depth + 1);
            case '"': {
                std::string s;
                if (!parseString(s)) return false;
                out = Json(std::move(s));
                return true;
            }
            case 't': return parseLiteral("true", out, Json(true));
            case 'f': return parseLiteral("false", out, Json(false));
            case 'n': return parseLiteral("null", out, Json(nullptr));
            default:
                if (c == '-' || isDigit(c)) return parseNumber(out);
                return fail("unexpected " + describeHere() + ", expected a value");
        }
    }

    bool parseLiteral(std::string_view word, Json& out, Json value) {
        if (text_.substr(pos_, word.size()) != word)
            return fail("invalid literal, expected '" + std::string(word) + "'");
        pos_ += word.size();
        out = std::move(value);
        return true;
    }

    bool parseObject(Json& out, int depth) {
        if (!checkDepth(depth)) return false;
        ++pos_;  // '{'
        out = Json::object();
        skipWhitespace();
        if (at('}')) {
            ++pos_;
            return true;
        }
        for (;;) {
            skipWhitespace();
            if (!more()) return fail("unexpected end of input inside an object");
            if (at('}')) return fail("trailing comma before '}'");
            if (!at('"')) return fail("unexpected " + describeHere() + ", expected a string key");
            const std::size_t keyPos = pos_;
            std::string key;
            if (!parseString(key)) return false;
            for (const std::string& existing : out.keys_)
                if (existing == key) return failAt(keyPos, "duplicate key '" + key + "'");
            skipWhitespace();
            if (!at(':')) return fail("unexpected " + describeHere() + ", expected ':' after a key");
            ++pos_;
            skipWhitespace();
            Json value;
            if (!parseValue(value, depth)) return false;
            out.keys_.push_back(std::move(key));
            out.items_.push_back(std::move(value));
            skipWhitespace();
            if (at('}')) {
                ++pos_;
                return true;
            }
            if (!more()) return fail("unexpected end of input inside an object");
            if (!at(',')) return fail("unexpected " + describeHere() + ", expected ',' or '}'");
            ++pos_;
        }
    }

    bool parseArray(Json& out, int depth) {
        if (!checkDepth(depth)) return false;
        ++pos_;  // '['
        out = Json::array();
        skipWhitespace();
        if (at(']')) {
            ++pos_;
            return true;
        }
        for (;;) {
            skipWhitespace();
            if (at(']')) return fail("trailing comma before ']'");
            Json value;
            if (!parseValue(value, depth)) return false;
            out.items_.push_back(std::move(value));
            skipWhitespace();
            if (at(']')) {
                ++pos_;
                return true;
            }
            if (!more()) return fail("unexpected end of input inside an array");
            if (!at(',')) return fail("unexpected " + describeHere() + ", expected ',' or ']'");
            ++pos_;
        }
    }

    bool parseString(std::string& out) {
        const std::size_t open = pos_;
        ++pos_;  // opening quote
        for (;;) {
            if (!more()) return failAt(open, "unterminated string");
            const std::uint8_t c = byte(pos_);
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c == '\\') {
                if (!parseEscape(out)) return false;
                continue;
            }
            if (c < 0x20) return fail("unescaped control character in string");
            if (c < 0x80) {
                out += static_cast<char>(c);
                ++pos_;
                continue;
            }
            const std::size_t len = utf8Length(pos_);
            if (len == 0) return fail("invalid UTF-8 sequence in string");
            out.append(text_.data() + pos_, len);
            pos_ += len;
        }
    }

    // Length of the well-formed UTF-8 sequence starting at `where`, or 0 when it is
    // malformed (truncated, overlong, a surrogate, or above U+10FFFF).
    std::size_t utf8Length(std::size_t where) const noexcept {
        const std::uint8_t lead = byte(where);
        std::size_t len = 0;
        std::uint32_t cp = 0;
        if (lead >= 0xC2 && lead <= 0xDF) {
            len = 2;
            cp = lead & 0x1Fu;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            len = 3;
            cp = lead & 0x0Fu;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            len = 4;
            cp = lead & 0x07u;
        } else {
            return 0;
        }
        if (text_.size() - where < len) return 0;
        for (std::size_t i = 1; i < len; ++i) {
            const std::uint8_t b = byte(where + i);
            if ((b & 0xC0) != 0x80) return 0;
            cp = (cp << 6) | (b & 0x3Fu);
        }
        if (len == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return 0;
        if (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) return 0;
        return len;
    }

    bool parseEscape(std::string& out) {
        const std::size_t start = pos_;
        ++pos_;  // backslash
        if (!more()) return failAt(start, "unterminated escape sequence");
        const char e = text_[pos_++];
        switch (e) {
            case '"': out += '"'; return true;
            case '\\': out += '\\'; return true;
            case '/': out += '/'; return true;
            case 'b': out += '\b'; return true;
            case 'f': out += '\f'; return true;
            case 'n': out += '\n'; return true;
            case 'r': out += '\r'; return true;
            case 't': out += '\t'; return true;
            case 'u': break;
            default: return failAt(start, "invalid escape sequence");
        }
        std::uint32_t cp = 0;
        if (!readHex4(cp)) return failAt(start, "invalid unicode escape, expected four hex digits");
        if (cp >= 0xDC00 && cp <= 0xDFFF)
            return failAt(start, "unpaired low surrogate in unicode escape");
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (!at('\\') || text_.size() - pos_ < 2 || text_[pos_ + 1] != 'u')
                return failAt(start, "unpaired high surrogate in unicode escape");
            pos_ += 2;
            std::uint32_t low = 0;
            if (!readHex4(low))
                return failAt(start, "invalid unicode escape, expected four hex digits");
            if (low < 0xDC00 || low > 0xDFFF)
                return failAt(start, "unpaired high surrogate in unicode escape");
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        }
        appendUtf8(out, cp);
        return true;
    }

    bool readHex4(std::uint32_t& value) noexcept {
        if (text_.size() - pos_ < 4) return false;
        std::uint32_t v = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const char h = text_[pos_ + i];
            std::uint32_t d = 0;
            if (h >= '0' && h <= '9') {
                d = static_cast<std::uint32_t>(h - '0');
            } else if (h >= 'a' && h <= 'f') {
                d = static_cast<std::uint32_t>(h - 'a' + 10);
            } else if (h >= 'A' && h <= 'F') {
                d = static_cast<std::uint32_t>(h - 'A' + 10);
            } else {
                return false;
            }
            v = (v << 4) | d;
        }
        pos_ += 4;
        value = v;
        return true;
    }

    static void appendUtf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool parseNumber(Json& out) {
        const std::size_t start = pos_;
        if (at('-')) ++pos_;
        if (!digitHere()) return failAt(start, "invalid number, expected a digit");
        if (at('0')) {
            ++pos_;
            if (digitHere()) return failAt(start, "invalid number, leading zeros are not allowed");
        } else {
            while (digitHere()) ++pos_;
        }
        if (at('.')) {
            ++pos_;
            if (!digitHere()) return failAt(start, "invalid number, expected a digit after '.'");
            while (digitHere()) ++pos_;
        }
        if (at('e') || at('E')) {
            ++pos_;
            if (at('+') || at('-')) ++pos_;
            if (!digitHere()) return failAt(start, "invalid number, expected a digit in the exponent");
            while (digitHere()) ++pos_;
        }
        double v = 0.0;
        if (!toDouble(text_.substr(start, pos_ - start), v))
            return failAt(start, "number is out of range");
        out = Json(v);
        return true;
    }

    static bool toDouble(std::string_view s, double& v) {
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
        const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        return r.ec == std::errc{} && r.ptr == s.data() + s.size() && std::isfinite(v);
#else
        // Standard libraries without floating-point from_chars: strtod in the "C" locale,
        // which the engine never changes.
        const std::string tmp(s);
        char* end = nullptr;
        v = std::strtod(tmp.c_str(), &end);
        return end == tmp.c_str() + tmp.size() && std::isfinite(v);
#endif
    }

    std::string_view text_;
    int maxDepth_;
    std::size_t pos_ = 0;
    std::size_t start_ = 0;  // first byte after an optional BOM
    std::size_t errorPos_ = 0;
    std::string error_;
};

} // namespace detail

// Parses a complete JSON document. On failure, value is null and error/line/column are set.
inline JsonParseResult parseJson(std::string_view text,
                                 int maxDepth = detail::JsonParser::kDefaultMaxDepth) {
    return detail::JsonParser(text, maxDepth).run();
}

// Reads and parses a whole file (binary mode, so CRLF files keep exact offsets). Files
// larger than maxBytes are rejected before they are read.
inline JsonParseResult loadJsonFile(const std::filesystem::path& path,
                                    int maxDepth = detail::JsonParser::kDefaultMaxDepth,
                                    std::uintmax_t maxBytes = std::uintmax_t{64} << 20) {
    JsonParseResult result;
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        result.error = "cannot open file: " + ec.message();
        return result;
    }
    if (size > maxBytes) {
        result.error = "file is larger than " + std::to_string(maxBytes) + " bytes";
        return result;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        result.error = "cannot open file";
        return result;
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    if (size != 0 && !in.read(text.data(), static_cast<std::streamsize>(size))) {
        result.error = "read error";
        return result;
    }
    return parseJson(text, maxDepth);
}

} // namespace lt::core

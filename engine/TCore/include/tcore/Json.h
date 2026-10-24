#pragma once
// TCore JSON value (Sections 21, 25, 26): the document model shared by the data-driven
// records under data/ and mods/ (items, enemies, events, biomes, bosses).
// - Objects keep insertion order, so dump() output is deterministic on every platform.
// - Lookups never throw: missing keys and out-of-range indices yield a shared null.
// - Parsing (strict grammar, line/column errors, bounded depth) lives in
//   tcore/JsonParser.h. Game saves still go through SaveFile, never raw JSON.
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <version>

namespace lt::core {

namespace detail {
class JsonParser;
} // namespace detail

class Json {
public:
    enum class Type : std::uint8_t { Null = 0, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) noexcept {}
    Json(bool b) noexcept : type_(Type::Bool), bool_(b) {}
    template <typename T>
        requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
    Json(T n) noexcept : type_(Type::Number), num_(static_cast<double>(n)) {}
    Json(const char* s) : type_(Type::String), str_(s != nullptr ? s : "") {}
    Json(std::string s) noexcept : type_(Type::String), str_(std::move(s)) {}
    Json(std::string_view s) : type_(Type::String), str_(s) {}

    static Json array() {
        Json j;
        j.type_ = Type::Array;
        return j;
    }
    static Json object() {
        Json j;
        j.type_ = Type::Object;
        return j;
    }

    static const char* nameOf(Type t) noexcept {
        switch (t) {
            case Type::Null: return "null";
            case Type::Bool: return "bool";
            case Type::Number: return "number";
            case Type::String: return "string";
            case Type::Array: return "array";
            case Type::Object: return "object";
        }
        return "unknown";
    }

    Type type() const noexcept { return type_; }
    const char* typeName() const noexcept { return nameOf(type_); }
    bool isNull() const noexcept { return type_ == Type::Null; }
    bool isBool() const noexcept { return type_ == Type::Bool; }
    bool isNumber() const noexcept { return type_ == Type::Number; }
    bool isInteger() const noexcept {
        return type_ == Type::Number && std::isfinite(num_) && num_ == std::floor(num_);
    }
    bool isString() const noexcept { return type_ == Type::String; }
    bool isArray() const noexcept { return type_ == Type::Array; }
    bool isObject() const noexcept { return type_ == Type::Object; }

    // Scalar access with a fallback when the value has a different type.
    bool asBool(bool def = false) const noexcept { return type_ == Type::Bool ? bool_ : def; }
    double asNumber(double def = 0.0) const noexcept { return type_ == Type::Number ? num_ : def; }
    std::int64_t asInt(std::int64_t def = 0) const noexcept {
        if (type_ != Type::Number || !std::isfinite(num_) || num_ < -9.0e18 || num_ > 9.0e18)
            return def;
        return static_cast<std::int64_t>(num_);
    }
    const std::string& str() const noexcept { return type_ == Type::String ? str_ : emptyString(); }
    std::string asString(std::string_view def = {}) const {
        return type_ == Type::String ? str_ : std::string(def);
    }

    // Containers. items() holds array elements or object values; keys() is parallel to
    // items() for objects.
    std::size_t size() const noexcept { return items_.size(); }
    bool empty() const noexcept { return items_.empty(); }
    const std::vector<Json>& items() const noexcept { return items_; }
    const std::vector<std::string>& keys() const noexcept { return keys_; }

    const Json& operator[](std::size_t i) const noexcept {
        return i < items_.size() ? items_[i] : nullValue();
    }
    const Json& operator[](std::string_view key) const noexcept {
        const Json* v = find(key);
        return v != nullptr ? *v : nullValue();
    }
    const Json* find(std::string_view key) const noexcept {
        if (type_ != Type::Object) return nullptr;
        for (std::size_t i = 0; i < keys_.size(); ++i)
            if (keys_[i] == key) return &items_[i];
        return nullptr;
    }
    Json* find(std::string_view key) noexcept {
        return const_cast<Json*>(static_cast<const Json&>(*this).find(key));
    }
    bool contains(std::string_view key) const noexcept { return find(key) != nullptr; }

    // Record helpers used by the data loaders (Section 21 schemas).
    double numberOr(std::string_view key, double def) const noexcept {
        return (*this)[key].asNumber(def);
    }
    std::int64_t intOr(std::string_view key, std::int64_t def) const noexcept {
        return (*this)[key].asInt(def);
    }
    bool boolOr(std::string_view key, bool def) const noexcept { return (*this)[key].asBool(def); }
    std::string stringOr(std::string_view key, std::string_view def) const {
        return (*this)[key].asString(def);
    }
    std::vector<std::string> stringList(std::string_view key) const {
        std::vector<std::string> out;
        const Json& a = (*this)[key];
        if (!a.isArray()) return out;
        out.reserve(a.items_.size());
        for (const Json& e : a.items_)
            if (e.isString()) out.push_back(e.str_);
        return out;
    }

    // Builders. A null value becomes an array/object on its first push/set.
    Json& push(Json v) {
        if (type_ == Type::Null) type_ = Type::Array;
        if (type_ == Type::Array) items_.push_back(std::move(v));
        return *this;
    }
    Json& set(std::string key, Json v) {
        if (type_ == Type::Null) type_ = Type::Object;
        if (type_ != Type::Object) return *this;
        for (std::size_t i = 0; i < keys_.size(); ++i) {
            if (keys_[i] == key) {
                items_[i] = std::move(v);
                return *this;
            }
        }
        keys_.push_back(std::move(key));
        items_.push_back(std::move(v));
        return *this;
    }
    bool erase(std::string_view key) {
        if (type_ != Type::Object) return false;
        for (std::size_t i = 0; i < keys_.size(); ++i) {
            if (keys_[i] == key) {
                keys_.erase(keys_.begin() + static_cast<std::ptrdiff_t>(i));
                items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        }
        return false;
    }

    // indent < 0: compact single line; indent >= 0: pretty-printed with that many spaces.
    std::string dump(int indent = -1) const {
        std::string out;
        dumpTo(out, indent, 0);
        return out;
    }

    // Deep equality; object member order does not matter.
    friend bool operator==(const Json& a, const Json& b) noexcept {
        if (a.type_ != b.type_) return false;
        switch (a.type_) {
            case Type::Null: return true;
            case Type::Bool: return a.bool_ == b.bool_;
            case Type::Number: return a.num_ == b.num_;
            case Type::String: return a.str_ == b.str_;
            case Type::Array:
                if (a.items_.size() != b.items_.size()) return false;
                for (std::size_t i = 0; i < a.items_.size(); ++i)
                    if (!(a.items_[i] == b.items_[i])) return false;
                return true;
            case Type::Object:
                if (a.items_.size() != b.items_.size()) return false;
                for (std::size_t i = 0; i < a.keys_.size(); ++i) {
                    const Json* other = b.find(a.keys_[i]);
                    if (other == nullptr || !(a.items_[i] == *other)) return false;
                }
                return true;
        }
        return false;
    }

private:
    friend class detail::JsonParser;

    static const Json& nullValue() noexcept {
        static const Json kNull;
        return kNull;
    }
    static const std::string& emptyString() noexcept {
        static const std::string kEmpty;
        return kEmpty;
    }

    static void newline(std::string& out, int indent, int level) {
        if (indent < 0) return;
        out += '\n';
        out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(level), ' ');
    }
    static void escaped(std::string& out, char c) {
        out += '\\';
        out += c;
    }
    static void dumpString(std::string& out, std::string_view s) {
        static constexpr char kHex[] = "0123456789abcdef";
        out += '"';
        for (const char ch : s) {
            const auto c = static_cast<unsigned char>(ch);
            switch (c) {
                case '"': escaped(out, '"'); break;
                case '\\': escaped(out, '\\'); break;
                case '\b': escaped(out, 'b'); break;
                case '\f': escaped(out, 'f'); break;
                case '\n': escaped(out, 'n'); break;
                case '\r': escaped(out, 'r'); break;
                case '\t': escaped(out, 't'); break;
                default:
                    if (c < 0x20) {
                        escaped(out, 'u');
                        out += "00";
                        out += kHex[c >> 4];
                        out += kHex[c & 0x0F];
                    } else {
                        out += ch;
                    }
                    break;
            }
        }
        out += '"';
    }
    static void dumpNumber(std::string& out, double v) {
        if (!std::isfinite(v)) {
            out += "null";
            return;
        }
        if (v == std::floor(v) && std::fabs(v) < 9007199254740992.0) {
            out += std::to_string(static_cast<long long>(v));
            return;
        }
        char buf[40];
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
        const auto r = std::to_chars(buf, buf + sizeof buf, v);
        out.append(buf, static_cast<std::size_t>(r.ptr - buf));
#else
        const int n = std::snprintf(buf, sizeof buf, "%.17g", v);
        if (n > 0) out.append(buf, static_cast<std::size_t>(n));
#endif
    }
    void dumpTo(std::string& out, int indent, int level) const {
        switch (type_) {
            case Type::Null: out += "null"; return;
            case Type::Bool: out += bool_ ? "true" : "false"; return;
            case Type::Number: dumpNumber(out, num_); return;
            case Type::String: dumpString(out, str_); return;
            case Type::Array:
            case Type::Object: {
                const bool obj = type_ == Type::Object;
                if (items_.empty()) {
                    out += obj ? "{}" : "[]";
                    return;
                }
                out += obj ? '{' : '[';
                for (std::size_t i = 0; i < items_.size(); ++i) {
                    if (i != 0) out += ',';
                    newline(out, indent, level + 1);
                    if (obj) {
                        dumpString(out, keys_[i]);
                        out += indent >= 0 ? ": " : ":";
                    }
                    items_[i].dumpTo(out, indent, level + 1);
                }
                newline(out, indent, level);
                out += obj ? '}' : ']';
                return;
            }
        }
    }

    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    std::vector<Json> items_;        // array elements, or object values in insertion order
    std::vector<std::string> keys_;  // object keys, parallel to items_
};

} // namespace lt::core

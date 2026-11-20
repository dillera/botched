// Minimal deterministic JSON value, parser and writer.
// Objects keep insertion order, so output is byte-stable (canonical form).
// Numbers are signed 64-bit integers only; the game never needs floats.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace botched {

class Json {
public:
    enum class Kind { Null, Bool, Int, String, Array, Object };
    using Array = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>;

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : kind_(Kind::Bool), int_(b ? 1 : 0) {}
    Json(int v) : kind_(Kind::Int), int_(v) {}
    Json(std::int64_t v) : kind_(Kind::Int), int_(v) {}
    Json(const char* s) : kind_(Kind::String), str_(s) {}
    Json(std::string s) : kind_(Kind::String), str_(std::move(s)) {}
    Json(Array a) : kind_(Kind::Array), arr_(std::move(a)) {}
    Json(Object o) : kind_(Kind::Object), obj_(std::move(o)) {}

    static Json array() { return Json(Array{}); }
    static Json object() { return Json(Object{}); }

    Kind kind() const { return kind_; }
    bool is_null() const { return kind_ == Kind::Null; }
    bool is_bool() const { return kind_ == Kind::Bool; }
    bool is_int() const { return kind_ == Kind::Int; }
    bool is_string() const { return kind_ == Kind::String; }
    bool is_array() const { return kind_ == Kind::Array; }
    bool is_object() const { return kind_ == Kind::Object; }

    bool as_bool() const { return int_ != 0; }
    std::int64_t as_int() const { return int_; }
    const std::string& as_string() const { return str_; }
    const Array& items() const { return arr_; }
    Array& items() { return arr_; }
    const Object& members() const { return obj_; }

    // Object helpers. set() replaces an existing key in place (keeps position).
    Json& set(std::string key, Json value);
    const Json* find(std::string_view key) const;
    void push(Json value) { arr_.push_back(std::move(value)); }

    // Typed lookups returning nullopt when missing or of the wrong type.
    std::optional<std::int64_t> get_int(std::string_view key) const;
    std::optional<std::string> get_string(std::string_view key) const;
    std::optional<bool> get_bool(std::string_view key) const;

    std::string dump() const;
    static std::optional<Json> parse(std::string_view text);

    friend bool operator==(const Json& a, const Json& b);

private:
    void write(std::string& out) const;

    Kind kind_ = Kind::Null;
    std::int64_t int_ = 0;
    std::string str_;
    Array arr_;
    Object obj_;
};

}  // namespace botched

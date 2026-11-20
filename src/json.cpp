#include "botched/json.hpp"

#include <cctype>
#include <unordered_set>

namespace botched {

Json& Json::set(std::string key, Json value) {
    kind_ = Kind::Object;
    for (auto& kv : obj_) {
        if (kv.first == key) { kv.second = std::move(value); return kv.second; }
    }
    obj_.emplace_back(std::move(key), std::move(value));
    return obj_.back().second;
}

const Json* Json::find(std::string_view key) const {
    if (kind_ != Kind::Object) return nullptr;
    for (const auto& kv : obj_) if (kv.first == key) return &kv.second;
    return nullptr;
}

std::optional<std::int64_t> Json::get_int(std::string_view key) const {
    const Json* j = find(key);
    if (!j || !j->is_int()) return std::nullopt;
    return j->as_int();
}
std::optional<std::string> Json::get_string(std::string_view key) const {
    const Json* j = find(key);
    if (!j || !j->is_string()) return std::nullopt;
    return j->as_string();
}
std::optional<bool> Json::get_bool(std::string_view key) const {
    const Json* j = find(key);
    if (!j || !j->is_bool()) return std::nullopt;
    return j->as_bool();
}

bool operator==(const Json& a, const Json& b) {
    if (a.kind_ != b.kind_) return false;
    switch (a.kind_) {
        case Json::Kind::Null: return true;
        case Json::Kind::Bool:
        case Json::Kind::Int: return a.int_ == b.int_;
        case Json::Kind::String: return a.str_ == b.str_;
        case Json::Kind::Array: return a.arr_ == b.arr_;
        case Json::Kind::Object: return a.obj_ == b.obj_;
    }
    return false;
}

namespace {

void write_string(std::string& out, const std::string& s) {
    static const char* hex = "0123456789abcdef";
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 15];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

struct Parser {
    std::string_view s;
    const ParseLimits& lim;
    std::size_t i = 0;
    int depth = 0;
    std::size_t nodes = 0;

    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
    bool eat(char c) { ws(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
    bool lit(std::string_view w) {
        if (s.substr(i, w.size()) == w) { i += w.size(); return true; }
        return false;
    }
    bool node() { return ++nodes <= lim.max_nodes; }

    std::optional<std::string> str() {
        if (i >= s.size() || s[i] != '"') return std::nullopt;
        ++i;
        std::string out;
        while (i < s.size()) {
            unsigned char c = static_cast<unsigned char>(s[i++]);
            if (c == '"') return out;
            if (out.size() >= lim.max_string) return std::nullopt;
            if (c == '\\') {
                if (i >= s.size()) return std::nullopt;
                char e = s[i++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        if (i + 4 > s.size()) return std::nullopt;
                        unsigned v = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = s[i++];
                            v <<= 4;
                            if (h >= '0' && h <= '9') v |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') v |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') v |= static_cast<unsigned>(h - 'A' + 10);
                            else return std::nullopt;
                        }
                        if (v > 0x7f) return std::nullopt;  // protocol identifiers are ASCII
                        out += static_cast<char>(v);
                        break;
                    }
                    default: return std::nullopt;
                }
            } else if (c < 0x20 || c > 0x7e) {
                return std::nullopt;  // raw control bytes, DEL and non-ASCII must not appear
            } else {
                out += static_cast<char>(c);
            }
        }
        return std::nullopt;
    }

    std::optional<Json> value() {
        ws();
        if (i >= s.size() || !node()) return std::nullopt;
        char c = s[i];
        if ((c == '{' || c == '[') && depth >= lim.max_depth) return std::nullopt;  // max_depth = nested containers
        if (c == '{') {
            ++i; ++depth;
            Json o = Json::object();
            std::unordered_set<std::string> seen;
            if (eat('}')) { --depth; return o; }
            for (;;) {
                ws();
                auto k = str();
                if (!k || !eat(':')) return std::nullopt;
                if (seen.size() >= lim.max_members || !seen.insert(*k).second) return std::nullopt;  // limit / duplicate
                auto v = value();
                if (!v) return std::nullopt;
                o.members().emplace_back(std::move(*k), std::move(*v));
                if (eat(',')) continue;
                if (eat('}')) break;
                return std::nullopt;
            }
            --depth;
            return o;
        }
        if (c == '[') {
            ++i; ++depth;
            Json a = Json::array();
            if (eat(']')) { --depth; return a; }
            for (;;) {
                if (a.items().size() >= lim.max_array) return std::nullopt;
                auto v = value();
                if (!v) return std::nullopt;
                a.push(std::move(*v));
                if (eat(',')) continue;
                if (eat(']')) break;
                return std::nullopt;
            }
            --depth;
            return a;
        }
        if (c == '"') {
            auto v = str();
            if (!v) return std::nullopt;
            return Json(std::move(*v));
        }
        if (lit("true")) return Json(true);
        if (lit("false")) return Json(false);
        if (lit("null")) return Json(nullptr);
        if (c == '-' || (c >= '0' && c <= '9')) {
            bool neg = c == '-';
            if (neg) ++i;
            if (i >= s.size() || s[i] < '0' || s[i] > '9') return std::nullopt;
            if (s[i] == '0' && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9') return std::nullopt;  // leading zero
            std::int64_t v = 0;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
                v = v * 10 + (s[i++] - '0');
                if (v > (std::int64_t{1} << 53)) return std::nullopt;  // beyond exact-integer range
            }
            if (i < s.size() && (s[i] == '.' || s[i] == 'e' || s[i] == 'E')) return std::nullopt;
            return Json(neg ? -v : v);
        }
        return std::nullopt;
    }
};

}  // namespace

void Json::write(std::string& out) const {
    switch (kind_) {
        case Kind::Null: out += "null"; break;
        case Kind::Bool: out += int_ ? "true" : "false"; break;
        case Kind::Int: out += std::to_string(int_); break;
        case Kind::String: write_string(out, str_); break;
        case Kind::Array: {
            out += '[';
            bool first = true;
            for (const auto& v : arr_) { if (!first) out += ','; first = false; v.write(out); }
            out += ']';
            break;
        }
        case Kind::Object: {
            out += '{';
            bool first = true;
            for (const auto& kv : obj_) {
                if (!first) out += ',';
                first = false;
                write_string(out, kv.first);
                out += ':';
                kv.second.write(out);
            }
            out += '}';
            break;
        }
    }
}

std::string Json::dump() const {
    std::string out;
    write(out);
    return out;
}

std::optional<Json> Json::parse(std::string_view text, const ParseLimits& limits) {
    if (text.size() > limits.max_bytes) return std::nullopt;
    Parser p{text, limits};
    auto v = p.value();
    if (!v) return std::nullopt;
    p.ws();
    if (p.i != text.size()) return std::nullopt;
    return v;
}

}  // namespace botched

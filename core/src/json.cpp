#include "fohcore/json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
namespace foh {
// ===================== Json =====================
static const Json kNull;
Json& Json::set(const std::string& k, Json v) {
    if (t != Object) *this = object();
    for (auto& m : o) if (m.first == k) { m.second = std::move(v); return m.second; }
    o.emplace_back(k, std::move(v)); return o.back().second;
}
const Json& Json::operator[](const std::string& k) const { if (t == Object) for (auto& m : o) if (m.first == k) return m.second; return kNull; }
bool Json::has(const std::string& k) const { if (t == Object) for (auto& m : o) if (m.first == k) return true; return false; }
bool Json::operator==(const Json& x) const {
    if (t != x.t) return false;
    switch (t) { case Null: return true; case Bool: return b == x.b; case Number: return n == x.n; case String: return s == x.s;
                 case Array: return a == x.a; case Object: return o == x.o; }
    return false;
}
static void esc(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) { switch (c) { case '"': out += "\\\""; break; case '\\': out += "\\\\"; break; case '\n': out += "\\n"; break; case '\t': out += "\\t"; break; case '\r': out += "\\r"; break;
        default: if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; } else out += (char) c; } }
    out += '"';
}
void Json::dumpTo(std::string& out, int ind, int d) const {
    auto nl = [&](int depth) { if (ind > 0) { out += '\n'; out.append((size_t) (depth * ind), ' '); } };
    switch (t) {
        case Null: out += "null"; break;
        case Bool: out += b ? "true" : "false"; break;
        case Number: { char buf[32]; if (std::floor(n) == n && std::fabs(n) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", n); else std::snprintf(buf, sizeof buf, "%.17g", n); out += buf; break; }
        case String: esc(out, s); break;
        case Array: if (a.empty()) { out += "[]"; break; } out += '['; for (size_t i = 0; i < a.size(); ++i) { if (i) out += ','; nl(d + 1); a[i].dumpTo(out, ind, d + 1); } nl(d); out += ']'; break;
        case Object: if (o.empty()) { out += "{}"; break; } out += '{'; for (size_t i = 0; i < o.size(); ++i) { if (i) out += ','; nl(d + 1); esc(out, o[i].first); out += ind > 0 ? ": " : ":"; o[i].second.dumpTo(out, ind, d + 1); } nl(d); out += '}'; break;
    }
}
std::string Json::dump(int indent) const { std::string s; dumpTo(s, indent, 0); return s; }

namespace {
struct Parser {
    const std::string& s; size_t i = 0; std::string err;
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
    bool fail(const char* m) { if (err.empty()) err = std::string(m) + " at " + std::to_string(i); return false; }
    bool lit(const char* w) { size_t n = std::char_traits<char>::length(w); if (s.compare(i, n, w) == 0) { i += n; return true; } return false; }
    bool str(std::string& out) {
        if (i >= s.size() || s[i] != '"') return fail("expected string");
        ++i;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c == '\\') { if (i >= s.size()) return fail("bad escape"); char e = s[i++];
                switch (e) { case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break; case 'b': out += '\b'; break; case 'f': out += '\f'; break;
                    case 'u': { if (i + 4 > s.size()) return fail("bad \\u"); unsigned cp = (unsigned) std::strtoul(s.substr(i, 4).c_str(), nullptr, 16); i += 4;
                        if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') { unsigned lo = (unsigned) std::strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16); i += 6; cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); }
                        if (cp < 0x80) out += (char) cp; else if (cp < 0x800) { out += (char) (0xC0 | (cp >> 6)); out += (char) (0x80 | (cp & 0x3F)); }
                        else if (cp < 0x10000) { out += (char) (0xE0 | (cp >> 12)); out += (char) (0x80 | ((cp >> 6) & 0x3F)); out += (char) (0x80 | (cp & 0x3F)); }
                        else { out += (char) (0xF0 | (cp >> 18)); out += (char) (0x80 | ((cp >> 12) & 0x3F)); out += (char) (0x80 | ((cp >> 6) & 0x3F)); out += (char) (0x80 | (cp & 0x3F)); } break; }
                    default: out += e; } }
            else out += c;
        }
        if (i >= s.size()) return fail("unterminated string");
        ++i; return true;
    }
    bool val(Json& v, int depth) {
        if (depth > 64) return fail("too deep");
        ws(); if (i >= s.size()) return fail("unexpected end");
        char c = s[i];
        if (c == '{') { ++i; v = Json::object(); ws(); if (i < s.size() && s[i] == '}') { ++i; return true; }
            for (;;) { ws(); std::string k; if (!str(k)) return false; ws(); if (i >= s.size() || s[i] != ':') return fail("expected :"); ++i;
                Json x; if (!val(x, depth + 1)) return false; v.set(k, std::move(x)); ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; } if (i < s.size() && s[i] == '}') { ++i; return true; } return fail("expected , or }"); } }
        if (c == '[') { ++i; v = Json::array(); ws(); if (i < s.size() && s[i] == ']') { ++i; return true; }
            for (;;) { Json x; if (!val(x, depth + 1)) return false; v.push(std::move(x)); ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; } if (i < s.size() && s[i] == ']') { ++i; return true; } return fail("expected , or ]"); } }
        if (c == '"') { std::string x; if (!str(x)) return false; v = Json(x); return true; }
        if (lit("true")) { v = Json(true); return true; } if (lit("false")) { v = Json(false); return true; } if (lit("null")) { v = Json(); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) { const char* st = s.c_str() + i; char* en = nullptr; double d = std::strtod(st, &en); if (en == st || !std::isfinite(d)) return fail("bad number"); i += (size_t) (en - st); v = Json(d); return true; }
        return fail("unexpected character");
    }
};
}
Json Json::parse(const std::string& text, std::string* error) {
    Parser p { text, 0, {} }; Json v;
    if (!p.val(v, 0)) { if (error) *error = p.err; return {}; }
    p.ws(); if (p.i != text.size()) { if (error) *error = "trailing characters at " + std::to_string(p.i); return {}; }
    if (error) error->clear();
    return v;
}

}

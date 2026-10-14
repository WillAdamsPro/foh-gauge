#pragma once
#include <string>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
namespace foh {
// ---- minimal JSON value (objects keep insertion order) ----
class Json {
public:
    enum Type { Null, Bool, Number, String, Array, Object };
    Json() = default;
    Json(bool v) : t(Bool), b(v) {}
    Json(int v) : t(Number), n(v) {}
    Json(double v) : t(Number), n(v) {}
    Json(const char* v) : t(String), s(v) {}
    Json(std::string v) : t(String), s(std::move(v)) {}
    static Json array() { Json j; j.t = Array; return j; }
    static Json object() { Json j; j.t = Object; return j; }
    Type type() const { return t; }
    bool isNull() const { return t == Null; } bool isObject() const { return t == Object; } bool isArray() const { return t == Array; }
    bool asBool(bool d = false) const { return t == Bool ? b : d; }
    double asNumber(double d = 0) const { return t == Number ? n : d; }
    int asInt(int d = 0) const { return t == Number && std::isfinite(n) && n >= std::numeric_limits<int>::min() && n <= std::numeric_limits<int>::max() ? (int) n : d; }
    std::string asString(const std::string& d = {}) const { return t == String ? s : d; }
    const std::vector<Json>& items() const { return a; }
    std::vector<Json>& items() { return a; }
    const std::vector<std::pair<std::string, Json>>& members() const { return o; }
    void push(Json v) { if (t != Array) { *this = array(); } a.push_back(std::move(v)); }
    Json& set(const std::string& k, Json v);
    const Json& operator[](const std::string& k) const;
    bool has(const std::string& k) const;
    std::string dump(int indent = 2) const;
    static Json parse(const std::string& text, std::string* error = nullptr);
    bool operator==(const Json& other) const;
private:
    void dumpTo(std::string& out, int indent, int depth) const;
    Type t = Null; bool b = false; double n = 0; std::string s; std::vector<Json> a; std::vector<std::pair<std::string, Json>> o;
};

}

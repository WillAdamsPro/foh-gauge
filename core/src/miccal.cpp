#include "fohcore/fohcore.h"
#include "fohcore/input.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <numeric>
#include <sstream>
namespace foh {
MicCal parseMicCal(const std::string& text) {
    MicCal c;
    auto bad = [&](const std::string& message) { MicCal failed; failed.error = message; return failed; };
    if (!input::validText(text)) return bad("Mic calibration must be UTF-8 text under 16 MiB");
    std::istringstream in(text.substr(text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0)); std::string line;
    std::vector<std::array<double,3>> rows; bool anyPhase = true; int nrows = 0;
    while (std::getline(in, line)) {
        std::string low = line; std::transform(low.begin(), low.end(), low.begin(), [](unsigned char ch){ return (char) std::tolower(ch); });
        auto sf = low.find("sens factor");
        if (sf != std::string::npos) {
            auto eq = low.find_first_of("=:", sf);
            if (eq == std::string::npos) return bad("Invalid Sens Factor");
            std::string value = low.substr(eq + 1); auto end = value.find_first_of("d,\"\r");
            if (end != std::string::npos) {
                if (value[end] == 'd' && (value.compare(end, 2, "db") != 0 || (end + 2 < value.size() && value[end + 2] != ',' && value[end + 2] != '"' && value[end + 2] != ' ' && value[end + 2] != '\r')))
                    return bad("Invalid Sens Factor units");
                value.resize(end);
            }
            std::istringstream trim(value); std::string token, extra; trim >> token;
            if ((trim >> extra) || !input::number(token, c.sensitivityOffsetDb) || std::abs(c.sensitivityOffsetDb) > 200)
                return bad("Invalid Sens Factor");
            c.hasSensFactor = true;
            std::string t = line; while (!t.empty() && (t.back() == '\r' || t.back() == '\n' || t.back() == ' ')) t.pop_back();
            size_t b = t.find_first_not_of(" \t\"\xEF\xBB\xBF"); t = b == std::string::npos ? "" : t.substr(b); if (!t.empty() && t.back() == '"') t.pop_back();
            c.sensFactorLine = t; continue; }
        size_t s = line.find_first_not_of(" \t\r\xEF\xBB\xBF"); if (s == std::string::npos) continue;
        char f = line[s]; if (f == '*' || f == '#' || f == ';' || f == '"' || f == '/') continue;
        size_t columnStart = 0;
        for (size_t i = 0; i <= line.size(); ++i) {
            if (i != line.size() && line[i] != ',' && line[i] != ';') continue;
            if (line.substr(columnStart, i - columnStart).find_first_not_of(" \t\r") == std::string::npos)
                return bad("Empty mic calibration column");
            columnStart = i + 1;
        }
        for (auto& ch : line) if (ch == ',' || ch == ';' || ch == '\t') ch = ' ';
        std::istringstream ls(line); std::vector<double> v; std::string tok;
        while (ls >> tok) {
            double d;
            if (!input::number(tok, d)) {
                // Conventional column headings are allowed, never partly numeric rows.
                if (v.empty() && (low.find("freq") != std::string::npos || low.find("hz") != std::string::npos)) break;
                return bad("Invalid mic calibration number on row " + std::to_string(nrows + 1));
            }
            v.push_back(d);
            if (v.size() > 3) return bad("Mic calibration needs two or three columns");
        }
        if (v.empty()) continue;
        if (v.size() < 2 || v[0] <= 0 || v[0] > 1000000 || std::abs(v[1]) > 200 || (v.size() == 3 && std::abs(v[2]) > 36000))
            return bad("Invalid mic calibration row or out-of-range value");
        if (rows.size() >= input::maxRows) return bad("Too many mic calibration rows");
        ++nrows; if (v.size() < 3) anyPhase = false;
        rows.push_back({v[0], v[1], v.size() >= 3 ? v[2] : 0.0});
    }
    std::stable_sort(rows.begin(), rows.end(), [](auto& a, auto& b){ return a[0] < b[0]; });
    for (auto& r : rows) { if (!c.hz.empty() && r[0] <= c.hz.back()) continue; c.hz.push_back(r[0]); c.db.push_back(r[1]); c.phaseDeg.push_back(r[2]); }
    c.hasPhase = nrows > 0 && anyPhase;
    if (c.hz.size() < 2) c.error = "No usable Hz,dB rows found";
    return c;
}
MicCal loadMicCal(const std::string& path) {
    try { return parseMicCal(input::read(path)); }
    catch (const std::exception& e) { MicCal c; c.error = e.what(); return c; }
}
static double interp(const std::vector<double>& hz, const std::vector<double>& v, double f) {
    if (hz.empty()) return 0; if (f <= hz.front()) return v.front(); if (f >= hz.back()) return v.back();
    auto it = std::upper_bound(hz.begin(), hz.end(), f); size_t i = (size_t) (it - hz.begin());
    double t = std::log(f / hz[i-1]) / std::log(hz[i] / hz[i-1]); return v[i-1] + t * (v[i] - v[i-1]);
}
double MicCal::dbAt(double f) const { return interp(hz, db, f); }
double MicCal::phaseAt(double f) const { return hasPhase ? interp(hz, phaseDeg, f) : 0.0; }
std::vector<double> MicCal::correctionDb(int n, double bin) const {
    std::vector<double> o((size_t) n); for (int k = 0; k < n; ++k) o[(size_t) k] = k == 0 ? 0 : -dbAt(k * bin); return o; }
void MicCal::apply(TFResult& tf) const {
    for (size_t k = 1; k < tf.H.size(); ++k) { double f = tf.freq[k];
        cpx m = std::polar(std::pow(10.0, dbAt(f) / 20.0), phaseAt(f) * M_PI / 180.0);
        tf.H[k] /= m; tf.magDb[k] = 20.0 * std::log10(std::abs(tf.H[k]) + 1e-12); tf.phaseDeg[k] = std::arg(tf.H[k]) * 180.0 / M_PI; }
}
}

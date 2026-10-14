#include "fohcore/fohcore.h"
#include "fohcore/input.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <locale>
#include <sstream>

namespace foh {
TargetTrace parseTarget(const std::string& text) {
    TargetTrace target;
    if (!input::validText(text)) { target.error = "Target must be UTF-8 text under 16 MiB"; return target; }
    std::istringstream input(text);
    std::string line;
    std::vector<std::array<double, 2>> rows;
    while (std::getline(input, line)) {
        if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        auto start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos) continue;
        line.erase(0, start);
        std::istringstream values(line);
        values.imbue(std::locale::classic());
        if (line.rfind("Tolerance:", 0) == 0) {
            values.ignore(10);
            if (!(values >> target.toleranceDb) || !std::isfinite(target.toleranceDb) || target.toleranceDb < 0 || target.toleranceDb > 200 || !(values >> std::ws).eof())
                target.error = "Invalid target tolerance";
            continue;
        }
        if (line.rfind("UseTolerance:", 0) == 0) {
            values.ignore(13); int enabled = 0;
            if (!(values >> enabled) || (enabled != 0 && enabled != 1) || !(values >> std::ws).eof()) target.error = "Invalid UseTolerance flag";
            target.useTolerance = enabled == 1;
            continue;
        }
        if (line[0] != '+' && line[0] != '-' && line[0] != '.' && (line[0] < '0' || line[0] > '9')) {
            std::string first, second; values >> first >> second;
            double numeric;
            if ((!rows.empty() || first == "nan" || first == "NaN" || first == "inf" || first == "Infinity" || (input::number(second, numeric) && (first.empty() || first.back() != ':')))
                && line[0] != '#' && line[0] != ';' && line[0] != '*') target.error = "Invalid target data row";
            continue;
        }
        // Smaart exports unavailable high-frequency bins as Hz * * *.
        std::istringstream missing(line); missing.imbue(std::locale::classic());
        double hz; std::string mag, phase, coherence, extra;
        if ((missing >> hz >> mag >> phase >> coherence) && !(missing >> extra)
            && std::isfinite(hz) && hz > 0 && hz <= 1000000 && mag == "*" && phase == "*" && coherence == "*") continue;
        std::vector<double> row; double value;
        while (values >> value) { row.push_back(value); if (row.size() > 4) break; }
        if (!values.eof() || (row.size() != 2 && row.size() != 4)
            || !std::all_of(row.begin(), row.end(), [](double v) { return std::isfinite(v); }) || row[0] <= 0 || row[0] > 1000000 || std::abs(row[1]) > 200
            || (row.size() == 4 && (std::abs(row[2]) > 36000 || row[3] < 0 || row[3] > 1))) {
            target.error = "Invalid target data row"; continue;
        }
        if (rows.size() >= input::maxRows) { target.error = "Too many target rows"; return target; }
        rows.push_back({row[0], row[1]});
    }
    std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a[0] < b[0]; });
    for (const auto& row : rows) {
        if (!target.hz.empty() && row[0] == target.hz.back()) continue;
        target.hz.push_back(row[0]); target.db.push_back(row[1]);
    }
    if (target.hz.size() < 2 && target.error.empty()) target.error = "No usable target trace found";
    return target;
}
}

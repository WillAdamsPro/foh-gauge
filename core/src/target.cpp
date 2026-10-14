#include "fohcore/fohcore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <locale>
#include <sstream>

namespace foh {
TargetTrace parseTarget(const std::string& text) {
    TargetTrace target;
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
            if (!(values >> target.toleranceDb) || !std::isfinite(target.toleranceDb) || target.toleranceDb < 0)
                target.error = "Invalid target tolerance";
            continue;
        }
        if (line.rfind("UseTolerance:", 0) == 0) {
            values.ignore(13); int enabled = 0;
            if (!(values >> enabled) || (enabled != 0 && enabled != 1)) target.error = "Invalid UseTolerance flag";
            target.useTolerance = enabled == 1;
            continue;
        }
        if (line[0] != '+' && line[0] != '-' && line[0] != '.' && (line[0] < '0' || line[0] > '9')) continue;
        // Smaart exports unavailable high-frequency bins as Hz * * *.
        std::istringstream missing(line); missing.imbue(std::locale::classic());
        double hz; std::string mag, phase, coherence, extra;
        if ((missing >> hz >> mag >> phase >> coherence) && !(missing >> extra)
            && std::isfinite(hz) && hz > 0 && mag == "*" && phase == "*" && coherence == "*") continue;
        std::vector<double> row; double value;
        while (values >> value) row.push_back(value);
        if (!values.eof() || (row.size() != 2 && row.size() != 4)
            || !std::all_of(row.begin(), row.end(), [](double v) { return std::isfinite(v); }) || row[0] <= 0) {
            target.error = "Invalid target data row"; continue;
        }
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

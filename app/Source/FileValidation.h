#pragma once
#include <fohcore/input.h>

namespace fohapp {
// Preflight before handing preference XML (including embedded device XML) to
// JUCE's recursive parser. This is a resource guard, not an XML parser.
inline bool boundedXml(const std::string& text) {
    if (text.empty() || text.size() > 1024 * 1024 || !foh::input::validText(text)) return false;
    int depth = 0; size_t tags = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '<') continue;
        if (++tags > 4096) return false;
        if (text.compare(i, 4, "<!--") == 0) {
            auto end = text.find("-->", i + 4); if (end == std::string::npos) return false; i = end + 2; continue;
        }
        // No DTD/external entities or CDATA are needed in saved preferences.
        if (i + 1 >= text.size() || text[i + 1] == '!') return false;
        size_t start = i; char quote = 0;
        for (++i; i < text.size(); ++i) {
            if (quote) { if (text[i] == quote) quote = 0; }
            else if (text[i] == '\'' || text[i] == '"') quote = text[i];
            else if (text[i] == '>') break;
        }
        if (i == text.size()) return false;
        if (text[start + 1] == '?') continue;
        if (text[start + 1] == '/') { if (--depth < 0) return false; }
        else if (text[i - 1] != '/' && ++depth > 32) return false;
    }
    return depth == 0 && tags > 0;
}
}

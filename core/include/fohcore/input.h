#pragma once
#include <cmath>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace foh::input {
// User-file parsing and I/O are message/worker-thread only. Bound both bytes and
// parsed records; never trust a file-size check alone (the file can grow).
inline constexpr size_t maxBytes = 16 * 1024 * 1024;
inline constexpr size_t maxRows = 131072;
inline std::string read(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > maxBytes)
        throw std::runtime_error("Missing or oversized file: " + path.filename().string());
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open " + path.filename().string());
    std::string text; char block[8192];
    while (file.read(block, sizeof block) || file.gcount()) {
        if (text.size() + (size_t) file.gcount() > maxBytes) throw std::runtime_error("File is too large");
        text.append(block, (size_t) file.gcount());
    }
    if (!file.eof()) throw std::runtime_error("Cannot read " + path.filename().string());
    return text;
}
inline bool validText(const std::string& text) {
    if (text.size() > maxBytes) return false;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = (unsigned char) text[i++];
        if (c < 0x80) { if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') return false; continue; }
        unsigned cp = 0; int count = 0;
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; count = 1; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; count = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; count = 3; }
        else return false;
        if (i + (size_t) count > text.size()) return false;
        for (int k = 0; k < count; ++k) { unsigned char b = (unsigned char) text[i++]; if ((b & 0xc0) != 0x80) return false; cp = (cp << 6) | (b & 63); }
        if ((count == 2 && cp < 0x800) || (count == 3 && cp < 0x10000) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}
inline bool number(const std::string& token, double& value) {
    std::istringstream stream(token); stream.imbue(std::locale::classic());
    return (stream >> value) && stream.peek() == std::char_traits<char>::eof() && std::isfinite(value);
}
}

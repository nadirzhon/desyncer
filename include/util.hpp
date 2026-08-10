// util.hpp — небольшие строковые помощники без зависимостей.
#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

namespace desyncer {

// Замена ВСЕХ вхождений `from` на `to`. Используется для подстановки
// плейсхолдеров {{HOST}} / {{PATH}} в шаблонах запросов.
inline std::string replace_all(std::string s, const std::string& from,
                               const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

inline bool ends_with(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Печатает байты с видимыми CR/LF — чтобы в отчёте было ясно,
// где стоит \r, где \n, а где голый LF. Это весь смысл инструмента.
inline std::string visualize(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() + 16);
    for (char c : raw) {
        switch (c) {
            case '\r': out += "\\r"; break;
            case '\n': out += "\\n\n"; break;
            case '\t': out += "\\t"; break;
            default:   out += c;
        }
    }
    return out;
}

}  // namespace desyncer

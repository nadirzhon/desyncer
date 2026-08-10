#include "scope_gate.hpp"
#include "util.hpp"
#include <fstream>

namespace desyncer {

bool ScopeGate::load(const std::string& path, std::string& err) {
    std::ifstream f(path);
    if (!f) {
        err = "не удалось открыть scope-файл: " + path;
        return false;
    }
    std::string line;
    while (std::getline(f, line)) {
        std::string h = to_lower(trim(line));
        if (h.empty() || h[0] == '#') continue;
        entries_.push_back(h);
    }
    if (entries_.empty()) {
        err = "scope-файл пуст: " + path;
        return false;
    }
    return true;
}

bool ScopeGate::in_scope(const std::string& host) const {
    std::string h = to_lower(host);
    for (const auto& e : entries_) {
        if (h == e) return true;
        // поддомен: host оканчивается на "." + запись
        if (ends_with(h, "." + e)) return true;
    }
    return false;
}

bool ScopeGate::may_send(const std::string& target_host,
                         const std::string& authorized_host,
                         std::string& reason) const {
    if (entries_.empty()) {
        reason = "scope-allowlist не загружен (--scope обязателен для отправки)";
        return false;
    }
    if (!in_scope(target_host)) {
        reason = "хост '" + target_host + "' вне scope-allowlist";
        return false;
    }
    if (to_lower(authorized_host) != to_lower(target_host)) {
        reason = "нет явного --i-am-authorized " + target_host
               + " (передано: '" + authorized_host + "')";
        return false;
    }
    return true;
}

}  // namespace desyncer

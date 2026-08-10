// scope_gate.hpp — трёхслойный authorized-only гейт.
//
// DESYNCER НИКОГДА не отправляет ни байта, пока цель не подтверждена:
//   (1) хост входит в scope-allowlist (сравнение по суффиксу домена);
//   (2) для этого хоста явно передан флаг --i-am-authorized;
//   иначе инструмент работает только в dry-run (печатает, что отправил бы).
//
// Fail-closed: любое сомнение → запрет отправки.
#pragma once
#include <string>
#include <vector>

namespace desyncer {

class ScopeGate {
public:
    // Загружает allowlist из файла: по одному суффиксу хоста на строку,
    // пустые строки и строки, начинающиеся с '#', игнорируются.
    // Возвращает false, если файл не открылся.
    bool load(const std::string& path, std::string& err);

    // Хост разрешён, если он равен записи allowlist или оканчивается на
    // ".<запись>" (поддомен). Регистр игнорируется.
    bool in_scope(const std::string& host) const;

    // Полная проверка перед отправкой. authorized_host — значение флага
    // --i-am-authorized (пустое, если флаг не передан).
    // При отказе reason содержит человекочитаемую причину.
    bool may_send(const std::string& target_host,
                  const std::string& authorized_host,
                  std::string& reason) const;

    size_t size() const { return entries_.size(); }

private:
    std::vector<std::string> entries_;  // нормализованы в нижний регистр
};

}  // namespace desyncer

// reporter.hpp — вывод результатов: таблица-вердикт в консоль,
// сырые req/resp и текстовый отчёт в файл для багбаунти-репорта.
#pragma once
#include "timing_oracle.hpp"
#include <string>
#include <vector>

namespace desyncer {

struct ReportContext {
    std::string host;
    std::string port;
    std::string path;
    int repeats = 0;
    int timeout_ms = 0;
    double baseline_median = 0.0;
    double baseline_sigma = 0.0;
    bool calibrated = false;
};

// Печатает человекочитаемую сводку в stdout.
void print_summary(const ReportContext& ctx,
                   const std::vector<ProbeResult>& results);

// Пишет подробный отчёт (вердикт + сырые req/resp подозрительных проб)
// в файл. Возвращает false при ошибке записи.
bool write_report(const std::string& path,
                  const ReportContext& ctx,
                  const std::vector<ProbeResult>& results,
                  std::string& err);

}  // namespace desyncer

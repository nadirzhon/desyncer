#include "reporter.hpp"
#include "desync_catalog.hpp"
#include "util.hpp"

#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace desyncer {

namespace {

const char* verdict_word(const ProbeResult& r) {
    if (r.errored) return "ОШИБКА";
    if (r.hanging) return "ВИСИТ";
    return "норма";
}

std::string fmt_ms(double v) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(0) << v;
    return os.str();
}

}  // namespace

void print_summary(const ReportContext& ctx,
                   const std::vector<ProbeResult>& results) {
    std::cout << "\n== DESYNCER ==\n";
    std::cout << "цель       : " << ctx.host << ":" << ctx.port
              << ctx.path << "\n";
    std::cout << "повторов   : " << ctx.repeats
              << "   таймаут: " << ctx.timeout_ms << " мс\n";
    if (ctx.calibrated) {
        std::cout << "база TTFB  : медиана " << fmt_ms(ctx.baseline_median)
                  << " мс, σ " << fmt_ms(ctx.baseline_sigma) << " мс\n";
    } else {
        std::cout << "база TTFB  : КАЛИБРОВКА НЕ УДАЛАСЬ (цель недоступна?)\n";
    }
    std::cout << "\n";

    // Заголовки — ASCII: std::setw считает байты, а кириллица в UTF-8
    // двухбайтовая и ломает выравнивание. Русский вердикт держим последним
    // столбцом (после него колонок нет, смещать нечего).
    std::cout << std::left
              << std::setw(18) << "probe"
              << std::setw(9)  << "kind"
              << std::right
              << std::setw(9)  << "TTFB,ms"
              << std::setw(9)  << "d-sigma"
              << std::setw(8)  << "t/out"
              << "   verdict\n";
    std::cout << std::string(64, '-') << "\n";

    int flagged = 0;
    for (const auto& r : results) {
        std::ostringstream sd;
        if (r.errored) sd << "-";
        else sd << std::fixed << std::setprecision(1) << r.sigma_delta << "s";
        std::ostringstream to;
        to << r.timeouts << "/" << r.repeats;
        std::cout << std::left
                  << std::setw(18) << r.probe.id
                  << std::setw(9)  << r.probe.kind
                  << std::right
                  << std::setw(9)  << (r.errored ? "-" : fmt_ms(r.probe_median))
                  << std::setw(9)  << sd.str()
                  << std::setw(8)  << to.str()
                  << "   " << verdict_word(r)
                  << "\n";
        if (r.hanging) ++flagged;
    }

    std::cout << "\n";
    if (flagged == 0) {
        std::cout << "[=] Признаков desync не обнаружено. "
                     "Это не гарантия отсутствия — попробуйте другой путь/порт "
                     "или ручную верификацию.\n";
    } else {
        std::cout << "[!] Подозрительных проб: " << flagged
                  << ". Сырые req/resp сохранены в отчёт для ручной проверки.\n";
        std::cout << "[!] Тайминг-оракул даёт КАНДИДАТОВ, а не подтверждение. "
                     "Верифицируйте вручную перед репортом.\n";
    }
}

bool write_report(const std::string& path,
                  const ReportContext& ctx,
                  const std::vector<ProbeResult>& results,
                  std::string& err) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        err = "не удалось открыть файл отчёта: " + path;
        return false;
    }

    f << "DESYNCER report\n";
    f << "target: " << ctx.host << ":" << ctx.port << ctx.path << "\n";
    f << "repeats: " << ctx.repeats << "  timeout_ms: " << ctx.timeout_ms << "\n";
    f << "baseline_median_ms: " << fmt_ms(ctx.baseline_median)
      << "  baseline_sigma_ms: " << fmt_ms(ctx.baseline_sigma) << "\n";
    f << std::string(72, '=') << "\n\n";

    for (const auto& r : results) {
        f << "[" << r.probe.id << "] " << r.probe.kind
          << " — " << r.probe.description << "\n";
        if (r.errored) {
            f << "  ошибка: " << r.error << "\n\n";
            continue;
        }
        f << "  TTFB медиана: " << fmt_ms(r.probe_median) << " мс"
          << "  (Δ " << std::fixed << std::setprecision(1) << r.sigma_delta << "σ)"
          << "  таймаутов: " << r.timeouts << "/" << r.repeats
          << "  вердикт: " << verdict_word(r) << "\n";

        // Сырые req/resp дампим для подозрительных проб — это и есть PoC.
        if (r.hanging) {
            std::string req = render(r.probe, ctx.host, ctx.path);
            f << "  --- RAW REQUEST (видимые CRLF) ---\n";
            f << visualize(req) << "\n";
            f << "  --- RAW RESPONSE (начало) ---\n";
            f << (r.sample.first_chunk.empty()
                    ? "(ответа нет — соединение зависло)"
                    : visualize(r.sample.first_chunk))
              << "\n";
        }
        f << "\n";
    }
    return true;
}

}  // namespace desyncer

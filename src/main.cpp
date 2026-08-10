// main.cpp — DESYNCER CLI.
//
// Authorized-only пробер HTTP desync / request smuggling.
// Отправляет байт-точные запросы на сырых сокетах и по тайминг-оракулу
// ищет расхождение front-end / back-end в разборе Content-Length vs
// Transfer-Encoding.
//
// ⚠ Только для целей, которые вы имеете право тестировать (свои системы,
//   bug-bounty scope с разрешением, договор на пентест). См. README.
#include "desync_catalog.hpp"
#include "reporter.hpp"
#include "scope_gate.hpp"
#include "timing_oracle.hpp"
#include "util.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace desyncer;

namespace {

struct Args {
    std::string target;          // host, host:port или URL
    std::string scope_file;      // --scope
    std::string authorized;      // --i-am-authorized
    std::string path = "/";      // --path
    std::string out = "desyncer_report.txt";  // --out
    std::string port;            // --port (перекрывает порт из URL)
    int repeats = 5;             // --repeats
    int timeout_ms = 5000;       // --timeout
    double sigma = 4.0;          // --sigma
    bool dry_run = false;        // --dry-run
    bool confirm_exploit = false;// --confirm-exploit (зарезервировано)
    bool help = false;
};

void usage() {
    std::cout <<
"DESYNCER 0.1 — authorized-only HTTP desync/smuggling пробер (C++/POSIX)\n\n"
"ИСПОЛЬЗОВАНИЕ:\n"
"  desyncer <target> --scope <file> --i-am-authorized <host> [опции]\n\n"
"ЦЕЛЬ:\n"
"  host | host:port | http://host[:port]/path\n\n"
"ОБЯЗАТЕЛЬНО ДЛЯ ОТПРАВКИ (иначе только dry-run):\n"
"  --scope <file>            allowlist доменов (суффиксы), по одному на строку\n"
"  --i-am-authorized <host>  явное подтверждение прав на ЭТОТ хост\n\n"
"ОПЦИИ:\n"
"  --path <p>       путь запроса (по умолчанию /)\n"
"  --port <n>       порт (перекрывает порт из URL; иначе 80)\n"
"  --repeats <n>    повторов на пробу (по умолчанию 5)\n"
"  --timeout <ms>   таймаут ответа (по умолчанию 5000)\n"
"  --sigma <x>      порог σ для вердикта «висит» (по умолчанию 4)\n"
"  --out <file>     файл отчёта (по умолчанию desyncer_report.txt)\n"
"  --dry-run        только показать запросы, ничего не отправлять\n"
"  --confirm-exploit  разблокировать агрессивные вектора (зарезервировано)\n"
"  -h, --help       эта справка\n\n"
"ЭТИКА: инструмент шлёт трафик только при пройденном scope-гейте И явном\n"
"       --i-am-authorized. Пробы неразрушающие (тайминг на своём соединении).\n"
"       Тестируйте лишь то, что имеете право тестировать.\n";
}

bool parse_target(const std::string& t, std::string& host, std::string& port,
                  std::string& path) {
    std::string s = t;
    if (s.rfind("http://", 0) == 0) s = s.substr(7);
    else if (s.rfind("https://", 0) == 0) {
        std::cerr << "[!] https:// пока не поддержан в MVP (нужен TLS). "
                     "Укажите host и порт 443 нельзя тестировать без TLS — "
                     "используйте http-цель.\n";
        s = s.substr(8);
    }
    // отделяем путь
    size_t slash = s.find('/');
    if (slash != std::string::npos) {
        path = s.substr(slash);
        s = s.substr(0, slash);
    }
    // host:port
    size_t colon = s.find(':');
    if (colon != std::string::npos) {
        host = s.substr(0, colon);
        port = s.substr(colon + 1);
    } else {
        host = s;
    }
    return !host.empty();
}

bool need_value(int i, int argc, const char* flag) {
    if (i + 1 >= argc) {
        std::cerr << "[!] флаг " << flag << " требует значения\n";
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string f = argv[i];
        auto val = [&](const char* name) -> std::string {
            if (!need_value(i, argc, name)) std::exit(2);
            return argv[++i];
        };
        if (f == "-h" || f == "--help") a.help = true;
        else if (f == "--scope") a.scope_file = val("--scope");
        else if (f == "--i-am-authorized") a.authorized = val("--i-am-authorized");
        else if (f == "--path") a.path = val("--path");
        else if (f == "--port") a.port = val("--port");
        else if (f == "--out") a.out = val("--out");
        else if (f == "--repeats") a.repeats = std::atoi(val("--repeats").c_str());
        else if (f == "--timeout") a.timeout_ms = std::atoi(val("--timeout").c_str());
        else if (f == "--sigma") a.sigma = std::atof(val("--sigma").c_str());
        else if (f == "--dry-run") a.dry_run = true;
        else if (f == "--confirm-exploit") a.confirm_exploit = true;
        else if (!f.empty() && f[0] == '-') {
            std::cerr << "[!] неизвестный флаг: " << f << "\n";
            return 2;
        } else {
            a.target = f;
        }
    }

    if (a.help || a.target.empty()) {
        usage();
        return a.target.empty() ? 2 : 0;
    }

    if (a.repeats < 1) a.repeats = 1;

    std::string host, port_from_url, path = a.path;
    if (!parse_target(a.target, host, port_from_url, path)) {
        std::cerr << "[!] не удалось разобрать цель: " << a.target << "\n";
        return 2;
    }
    std::string port = !a.port.empty() ? a.port
                     : (!port_from_url.empty() ? port_from_url : "80");

    // Стартовый баннер о легальности.
    std::cerr << "DESYNCER — только для авторизованного тестирования. "
                 "Отправка трафика без прав незаконна.\n";

    auto catalog = default_catalog();

    // --- Гейт авторизации ------------------------------------------------
    bool allowed = false;
    if (!a.dry_run) {
        ScopeGate gate;
        std::string err;
        if (a.scope_file.empty()) {
            std::cerr << "[gate] --scope не задан → перехожу в dry-run "
                         "(отправки не будет)\n";
            a.dry_run = true;
        } else if (!gate.load(a.scope_file, err)) {
            std::cerr << "[gate] " << err << " → dry-run\n";
            a.dry_run = true;
        } else {
            std::string reason;
            if (gate.may_send(host, a.authorized, reason)) {
                allowed = true;
            } else {
                std::cerr << "[gate] отказ: " << reason
                          << " → dry-run\n";
                a.dry_run = true;
            }
        }
    }

    // --- Dry-run: показать запросы и выйти -------------------------------
    if (a.dry_run || !allowed) {
        std::cout << "\n[dry-run] запросы, которые были бы отправлены на "
                  << host << ":" << port << path << ":\n";
        for (const auto& p : catalog) {
            std::cout << "\n----- " << p.id << " (" << p.kind << ") — "
                      << p.description << " -----\n";
            std::cout << visualize(render(p, host, path)) << "\n";
        }
        std::cout << "\n[dry-run] ничего не отправлено. Для реального прогона: "
                     "--scope <file> --i-am-authorized " << host << "\n";
        return 0;
    }

    // --- Реальный прогон -------------------------------------------------
    std::cout << "[gate] авторизация пройдена для " << host
              << " — начинаю пробы\n";

    OracleConfig cfg;
    cfg.host = host;
    cfg.port = port;
    cfg.path = path;
    cfg.repeats = a.repeats;
    cfg.timeout_ms = a.timeout_ms;
    cfg.sigma_threshold = a.sigma;

    TimingOracle oracle(cfg);
    auto results = oracle.run(catalog);

    ReportContext ctx;
    ctx.host = host;
    ctx.port = port;
    ctx.path = path;
    ctx.repeats = a.repeats;
    ctx.timeout_ms = a.timeout_ms;
    ctx.baseline_median = oracle.baseline_median();
    ctx.baseline_sigma = oracle.baseline_sigma();
    ctx.calibrated = oracle.calibrated();

    print_summary(ctx, results);

    std::string err;
    if (write_report(a.out, ctx, results, err)) {
        std::cout << "[+] отчёт записан: " << a.out << "\n";
    } else {
        std::cerr << "[!] " << err << "\n";
    }
    return 0;
}

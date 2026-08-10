#include "desync_catalog.hpp"
#include "util.hpp"

namespace desyncer {

// Замечание по семантике тайминг-проб (James Kettle, «HTTP Desync Attacks»):
//
//   CL.TE — front-end доверяет Content-Length, back-end — Transfer-Encoding.
//           Пейлоад сделан так, что back-end (TE) после первого чанка ждёт
//           продолжения, которого front-end (CL) уже не пришлёт → back-end
//           «висит». Задержка ⇒ вероятен CL.TE.
//
//   TE.CL — наоборот: front-end (TE) завершает по нулевому чанку, back-end
//           (CL) ждёт больше байт, чем пришло → «висит». Задержка ⇒ TE.CL.
//
// Все пробы отправляются на ОТДЕЛЬНОМ соединении и лишь измеряют его
// задержку. Никакой отравляющий префикс в чужой keep-alive не инжектится.

std::vector<Probe> default_catalog() {
    std::vector<Probe> c;

    // --- Базовый корректный запрос: эталон времени ответа ---------------
    c.push_back({
        "baseline", "baseline",
        "Корректный GET — эталон нормального времени ответа",
        "GET {{PATH}} HTTP/1.1\r\n"
        "Host: {{HOST}}\r\n"
        "Connection: close\r\n"
        "User-Agent: desyncer/0.1 (authorized-testing)\r\n"
        "\r\n",
        false});

    // Тело CL.TE-пробы: chunk size 1, один байт, БЕЗ финального 0-чанка.
    // Back-end на TE зависнет в ожидании следующего чанка.
    const std::string clte_body = "1\r\nA\r\nX";
    // Тело TE.CL-пробы: корректно закрытый чанковый поток (5 байт),
    // но Content-Length объявлен больше → back-end на CL ждёт добавки.
    const std::string tecl_body = "0\r\n\r\n";

    // --- CL.TE с разными обфускациями Transfer-Encoding -----------------
    struct TE { std::string id, desc, header; };
    const std::vector<TE> te_variants = {
        {"te-plain",   "обычный Transfer-Encoding: chunked",
                       "Transfer-Encoding: chunked\r\n"},
        {"te-space",   "пробел перед двоеточием (TE :)",
                       "Transfer-Encoding : chunked\r\n"},
        {"te-tab",     "таб после двоеточия",
                       "Transfer-Encoding:\tchunked\r\n"},
        {"te-nospace", "без пробела после двоеточия",
                       "Transfer-Encoding:chunked\r\n"},
        {"te-vtab",    "вертикальный таб перед значением",
                       "Transfer-Encoding: \x0bchunked\r\n"},
        {"te-fold",    "продолжение заголовка через ведущий пробел (obs-fold)",
                       "X-Fold: pad\r\n Transfer-Encoding: chunked\r\n"},
        {"te-dup",     "дублированный Transfer-Encoding",
                       "Transfer-Encoding: chunked\r\n"
                       "Transfer-Encoding: identity\r\n"},
    };

    for (const auto& te : te_variants) {
        std::string body_len = std::to_string(4);  // CL=4 отрезает "1\r\nA"
        c.push_back({
            "clte-" + te.id, "CL.TE",
            "CL.TE: " + te.desc,
            "POST {{PATH}} HTTP/1.1\r\n"
            "Host: {{HOST}}\r\n"
            + te.header +
            "Content-Length: " + body_len + "\r\n"
            "Connection: close\r\n"
            "\r\n"
            + clte_body,
            false});
    }

    // --- TE.CL: front-end по TE завершает, back-end по CL ждёт добавки --
    for (const auto& te : {te_variants[0], te_variants[1], te_variants[2]}) {
        c.push_back({
            "tecl-" + te.id, "TE.CL",
            "TE.CL: " + te.desc,
            "POST {{PATH}} HTTP/1.1\r\n"
            "Host: {{HOST}}\r\n"
            + te.header +
            "Content-Length: 6\r\n"   // больше, чем 5 байт тела → back-end ждёт
            "Connection: close\r\n"
            "\r\n"
            + tecl_body,
            false});
    }

    // --- LF-only переводы строк (без CR) --------------------------------
    // Некоторые прокси принимают голый LF как разделитель, back-end — нет.
    c.push_back({
        "clte-lf-only", "CL.TE",
        "CL.TE: разделители строк — голый LF без CR",
        "POST {{PATH}} HTTP/1.1\n"
        "Host: {{HOST}}\n"
        "Transfer-Encoding: chunked\n"
        "Content-Length: 4\n"
        "Connection: close\n"
        "\n"
        + clte_body,
        false});

    return c;
}

std::string render(const Probe& p, const std::string& host,
                   const std::string& path) {
    std::string s = replace_all(p.tmpl, "{{HOST}}", host);
    s = replace_all(s, "{{PATH}}", path);
    return s;
}

}  // namespace desyncer

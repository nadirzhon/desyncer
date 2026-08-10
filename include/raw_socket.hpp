// raw_socket.hpp — байт-точный TCP-клиент.
//
// Отправляет запрос РОВНО так, как задано (никакой нормализации заголовков,
// в отличие от любой HTTP-библиотеки), и измеряет время до первого байта
// ответа (TTFB) по монотонным часам.
//
// Одно соединение на один вызов = safe-mode: мы измеряем только СВОЁ
// соединение и никогда не инжектируем префикс в чужой keep-alive.
#pragma once
#include <string>

namespace desyncer {

struct Measurement {
    double ttfb_ms = 0.0;      // время до первого байта ответа после полной отправки
    bool timed_out = false;    // ответ не пришёл за timeout_ms
    bool connect_failed = false;
    std::string first_chunk;   // начало ответа (до нескольких КБ) для отчёта
    std::string error;
};

// host — доменное имя или IP; port — строкой ("80"); request — сырые байты.
Measurement send_and_measure(const std::string& host,
                             const std::string& port,
                             const std::string& request,
                             int timeout_ms);

}  // namespace desyncer

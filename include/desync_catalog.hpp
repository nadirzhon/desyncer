// desync_catalog.hpp — канонические пробы request smuggling / desync.
//
// Каждая проба хранит СЫРОЙ шаблон запроса с явными \r\n, \n и {{HOST}},
// {{PATH}} — так, чтобы в исходнике было видно каждый байт на проводе.
// Именно эти байты HTTP-библиотеки молча «чинят» и потому не могут
// воспроизвести десинк.
//
// Детектирование — по неразрушающему тайминг-оракулу: неоднозначный
// запрос, который один парсер завершает, а другой — ждёт продолжения,
// заставляет back-end «висеть». Всё измеряется на нашем собственном
// соединении (safe-mode), без инъекции префикса в чужой keep-alive.
#pragma once
#include <string>
#include <vector>

namespace desyncer {

struct Probe {
    std::string id;           // короткий идентификатор
    std::string kind;         // "baseline" | "CL.TE" | "TE.CL"
    std::string description;  // что именно проверяет
    std::string tmpl;         // сырой шаблон с плейсхолдерами
    bool aggressive = false;  // требует --confirm-exploit
};

// Полный каталог: базовый (корректный) запрос + пробы с обфускацией
// Transfer-Encoding и дублированием Content-Length.
std::vector<Probe> default_catalog();

// Подставляет {{HOST}} и {{PATH}} в шаблон, возвращает готовые байты.
std::string render(const Probe& p, const std::string& host,
                   const std::string& path);

}  // namespace desyncer

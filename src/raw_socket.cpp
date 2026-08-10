#include "raw_socket.hpp"

#include <chrono>
#include <cstring>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace desyncer {

namespace {

using clock_t_ = std::chrono::steady_clock;  // это CLOCK_MONOTONIC на большинстве систем

double ms_since(clock_t_::time_point t0) {
    auto d = clock_t_::now() - t0;
    return std::chrono::duration<double, std::milli>(d).count();
}

// Открывает TCP-соединение, возвращает fd или -1. TCP_NODELAY включён,
// чтобы мы полностью контролировали сегментацию и не давали ядру
// склеивать/задерживать наши байты.
int connect_tcp(const std::string& host, const std::string& port,
                std::string& err) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
    if (rc != 0) {
        err = std::string("getaddrinfo: ") + gai_strerror(rc);
        return -1;
    }

    int fd = -1;
    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        err = "не удалось установить TCP-соединение";
        return -1;
    }

    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    return fd;
}

// Гарантированно отправляет весь буфер.
bool send_all(int fd, const std::string& data, std::string& err) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            err = std::string("send: ") + std::strerror(errno);
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

}  // namespace

Measurement send_and_measure(const std::string& host,
                             const std::string& port,
                             const std::string& request,
                             int timeout_ms) {
    Measurement m;

    std::string err;
    int fd = connect_tcp(host, port, err);
    if (fd < 0) {
        m.connect_failed = true;
        m.error = err;
        return m;
    }

    if (!send_all(fd, request, err)) {
        m.error = err;
        ::close(fd);
        return m;
    }

    // С этого момента считаем время до первого байта ответа.
    auto t0 = clock_t_::now();

    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;

    int pr = ::poll(&pfd, 1, timeout_ms);
    if (pr == 0) {
        // Никакого ответа за отведённое время — соединение «висит».
        // Именно это отличает опасный desync-пейлоад от обычного запроса.
        m.timed_out = true;
        m.ttfb_ms = static_cast<double>(timeout_ms);
        ::close(fd);
        return m;
    }
    if (pr < 0) {
        m.error = std::string("poll: ") + std::strerror(errno);
        ::close(fd);
        return m;
    }

    m.ttfb_ms = ms_since(t0);

    // Дочитываем начало ответа для отчёта (без блокировки надолго).
    char buf[4096];
    std::string resp;
    for (int i = 0; i < 4; ++i) {
        int p2 = ::poll(&pfd, 1, 200);
        if (p2 <= 0) break;
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        resp.append(buf, static_cast<size_t>(n));
        if (resp.size() >= 8192) break;
    }
    m.first_chunk = resp;

    ::close(fd);
    return m;
}

}  // namespace desyncer

// timing_oracle.hpp — статистический детектор «зависаний».
//
// Для каждой пробы: N повторов измерения TTFB на свежих соединениях,
// затем медиана и σ. Проба считается «висящей» (⇒ вероятный desync),
// если её медианный TTFB значимо превышает базовый — по числу σ или по
// доле таймаутов. Медиана + σ вместо оценки «на глаз» отсекает джиттер сети.
#pragma once
#include "desync_catalog.hpp"
#include "raw_socket.hpp"
#include <string>
#include <vector>

namespace desyncer {

struct ProbeResult {
    Probe probe;
    double baseline_median = 0.0;
    double baseline_sigma = 0.0;
    double probe_median = 0.0;
    double sigma_delta = 0.0;   // (probe_median - baseline_median) / baseline_sigma
    int timeouts = 0;
    int repeats = 0;
    bool hanging = false;       // итоговый вердикт по пробе
    bool errored = false;
    std::string error;
    Measurement sample;         // один прогон целиком — для сырого дампа
};

struct OracleConfig {
    std::string host;
    std::string port = "80";
    std::string path = "/";
    int repeats = 5;
    int timeout_ms = 5000;
    double sigma_threshold = 4.0;  // во сколько σ выше базы = «висит»
    double min_abs_gap_ms = 400.0; // и не меньше стольких мс абсолютной разницы
};

class TimingOracle {
public:
    explicit TimingOracle(OracleConfig cfg) : cfg_(std::move(cfg)) {}

    // Сначала калибрует базовую линию (по пробе kind=="baseline"),
    // затем прогоняет остальные пробы. progress вызывается для каждой.
    std::vector<ProbeResult> run(const std::vector<Probe>& catalog);

    double baseline_median() const { return baseline_median_; }
    double baseline_sigma() const { return baseline_sigma_; }
    bool calibrated() const { return calibrated_; }

private:
    OracleConfig cfg_;
    double baseline_median_ = 0.0;
    double baseline_sigma_ = 0.0;
    bool calibrated_ = false;

    std::vector<double> measure_series(const std::string& request,
                                       int& timeouts, Measurement& sample);
};

}  // namespace desyncer

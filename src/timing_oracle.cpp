#include "timing_oracle.hpp"
#include <algorithm>
#include <cmath>

namespace desyncer {

namespace {

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return (n % 2) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

double stdev(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    double mean = 0.0;
    for (double x : v) mean += x;
    mean /= static_cast<double>(v.size());
    double acc = 0.0;
    for (double x : v) acc += (x - mean) * (x - mean);
    return std::sqrt(acc / static_cast<double>(v.size() - 1));
}

}  // namespace

std::vector<double> TimingOracle::measure_series(const std::string& request,
                                                 int& timeouts,
                                                 Measurement& sample) {
    std::vector<double> series;
    timeouts = 0;
    for (int i = 0; i < cfg_.repeats; ++i) {
        Measurement m = send_and_measure(cfg_.host, cfg_.port, request,
                                         cfg_.timeout_ms);
        if (i == 0) sample = m;
        if (m.connect_failed) {
            sample = m;
            return {};  // соединение не встало — серия недействительна
        }
        if (m.timed_out) ++timeouts;
        series.push_back(m.ttfb_ms);
    }
    return series;
}

std::vector<ProbeResult> TimingOracle::run(const std::vector<Probe>& catalog) {
    // 1) Калибровка базовой линии.
    for (const auto& p : catalog) {
        if (p.kind != "baseline") continue;
        std::string req = render(p, cfg_.host, cfg_.path);
        int to = 0;
        Measurement s;
        auto series = measure_series(req, to, s);
        if (!series.empty()) {
            baseline_median_ = median(series);
            baseline_sigma_ = stdev(series);
            // защита от нулевой σ (идеально стабильная сеть): минимум 1 мс
            if (baseline_sigma_ < 1.0) baseline_sigma_ = 1.0;
            calibrated_ = true;
        }
        break;
    }

    // 2) Прогон проб.
    std::vector<ProbeResult> out;
    for (const auto& p : catalog) {
        if (p.kind == "baseline") continue;

        ProbeResult r;
        r.probe = p;
        r.repeats = cfg_.repeats;
        r.baseline_median = baseline_median_;
        r.baseline_sigma = baseline_sigma_;

        std::string req = render(p, cfg_.host, cfg_.path);
        int to = 0;
        auto series = measure_series(req, to, r.sample);

        if (series.empty()) {
            r.errored = true;
            r.error = r.sample.error.empty() ? "серия измерений пуста"
                                             : r.sample.error;
            out.push_back(r);
            continue;
        }

        r.timeouts = to;
        r.probe_median = median(series);
        r.sigma_delta = calibrated_
            ? (r.probe_median - baseline_median_) / baseline_sigma_
            : 0.0;

        // Вердикт: либо большинство прогонов ушло в таймаут,
        // либо медиана значимо (в σ И в абсолюте) выше базовой.
        bool many_timeouts = to * 2 > cfg_.repeats;
        bool slow = calibrated_
            && r.sigma_delta >= cfg_.sigma_threshold
            && (r.probe_median - baseline_median_) >= cfg_.min_abs_gap_ms;
        r.hanging = many_timeouts || slow;

        out.push_back(r);
    }
    return out;
}

}  // namespace desyncer

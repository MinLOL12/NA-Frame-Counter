#include "NandlCalculator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace na_frame_counter::math {

namespace {

constexpr double kInvTwoSqrtTwo = 0.3535533905932737622; // 1 / (2 * sqrt(2))
constexpr double kErfcCutoff = 8.5; // erfc(8.5) ~ 2.6e-33
constexpr double kLutMaxX = 6.0;
constexpr std::size_t kLutSamples = 100000;
constexpr double kLutStep = kLutMaxX / static_cast<double>(kLutSamples);
constexpr double kLutInvStep = static_cast<double>(kLutSamples) / kLutMaxX;

std::array<double, kLutSamples + 2> const& getErfcLut() {
    static auto const* lut = []() {
        auto* arr = new std::array<double, kLutSamples + 2>();
        for (std::size_t i = 0; i <= kLutSamples; ++i) {
            (*arr)[i] = std::erfc(static_cast<double>(i) * kLutStep);
        }
        (*arr)[kLutSamples + 1] = std::erfc(kLutMaxX);
        return arr;
    }();
    return *lut;
}

inline double fastErfc(double x) {
    if (x <= 0.0) return 1.0;
    if (x > kErfcCutoff) return 0.0;
    if (x >= kLutMaxX) return std::erfc(x);
    auto const& lut = getErfcLut();
    const double scaled = x * kLutInvStep;
    const auto idx = static_cast<std::size_t>(scaled);
    const double frac = scaled - static_cast<double>(idx);
    return lut[idx] + frac * (lut[idx + 1] - lut[idx]);
}

struct PreparedInputs {
    std::vector<double> times;
    std::vector<double> weights; // negative (< 0.0) denotes NaNDL '-' ignored input (p = 1, q = 0)
    bool hasActiveWeight = false;
};

double sanitizePositive(double v, double fallback) {
    return (std::isfinite(v) && v > 0.0) ? v : fallback;
}

double sanitizeNonNegative(double v, double fallback = 0.0) {
    return (std::isfinite(v) && v >= 0.0) ? v : fallback;
}

double evaluateInternal(
    std::vector<double> const& times,
    std::vector<double> const& weights,
    double lValue,
    bool useLut,
    double* outPassProb = nullptr,
    double* outAttemptTime = nullptr
) {
    if (times.empty() || times.size() != weights.size() || !std::isfinite(lValue) || lValue < 0.0) {
        if (outPassProb) *outPassProb = 0.0;
        if (outAttemptTime) *outAttemptTime = 0.0;
        return 1e100;
    }

    double r = 1.0;
    double failTimeSum = 0.0;
    const std::size_t n = times.size();
    const double tLast = times[n - 1];

    for (std::size_t j = 0; j < n; ++j) {
        if (weights[j] < 0.0) {
            continue; // Ignored '-' input: 100% pass (p_j = 1, q_j = 0)
        }
        const double x = weights[j] * lValue;
        if (!std::isfinite(x)) {
            if (outPassProb) *outPassProb = 0.0;
            if (outAttemptTime) *outAttemptTime = 0.0;
            return 1e100;
        }
        if (x > kErfcCutoff) {
            continue; // q_j < 1e-32, p_j == 1
        }
        const double q = useLut ? fastErfc(x) : std::erfc(x);
        double p = 1.0 - q;
        if (p < 1e-18) p = 1e-18;

        failTimeSum += times[j] * r * q;
        r *= p;
        if (r < 1e-250) {
            if (outPassProb) *outPassProb = 0.0;
            if (outAttemptTime) *outAttemptTime = failTimeSum;
            return 1e100;
        }
    }

    const double eTa = tLast * r + failTimeSum;
    const double eTc = eTa / r;
    if (outPassProb) *outPassProb = r;
    if (outAttemptTime) *outAttemptTime = eTa;
    if (!std::isfinite(eTc)) return 1e100;
    return eTc;
}

PreparedInputs prepareInputs(
    std::vector<InputTiming> const& inputs,
    NandlParams const& params,
    NandlModel model
) {
    PreparedInputs out;
    if (inputs.empty()) return out;

    const double fps = sanitizePositive(params.macroFps, 240.0);
    const double respawn = sanitizeNonNegative(params.respawnTime, 0.0);
    const double kT = sanitizeNonNegative(params.kT, 0.0);
    const double kU = sanitizeNonNegative(params.kU, 0.0);
    const double kC = sanitizeNonNegative(params.kC, 0.0);

    const bool useNerve = (model == NandlModel::Nerve || model == NandlModel::NerveFatigue ||
                           model == NandlModel::NerveCPS || model == NandlModel::NFC);
    const bool useFatigue = (model == NandlModel::Fatigue || model == NandlModel::NerveFatigue ||
                             model == NandlModel::FatigueCPS || model == NandlModel::NFC);
    const bool useCps = (model == NandlModel::CPS || model == NandlModel::NerveCPS ||
                         model == NandlModel::FatigueCPS || model == NandlModel::NFC);

    struct FlatItem {
        double time;
        std::int64_t index;
        double windowFrames;
        bool ignored;
    };

    std::size_t totalFlat = 0;
    for (auto const& item : inputs) {
        totalFlat += static_cast<std::size_t>(std::max(1, item.swiftCount));
    }

    std::vector<FlatItem> flat;
    flat.reserve(totalFlat);

    std::int64_t runningIndex = 1;
    for (auto const& item : inputs) {
        const double baseFrame = std::max(0.0, item.frame);
        const bool isIgnored = item.ignored;
        const double fw = (!isIgnored && std::isfinite(item.windowFrames) && item.windowFrames > 0.0)
            ? item.windowFrames
            : (isIgnored ? 0.0 : 1.0);
        const int swift = std::max(1, item.swiftCount);

        if (swift == 1) {
            flat.push_back({respawn + baseFrame / fps, runningIndex++, fw, isIgnored});
        } else if (isIgnored) {
            for (int s = 0; s < swift; ++s) {
                const double subFrame = baseFrame + static_cast<double>(s) / static_cast<double>(swift);
                flat.push_back({respawn + subFrame / fps, runningIndex++, 0.0, true});
            }
        } else {
            const double wMain = std::max(fw - static_cast<double>(swift - 1) / swift, 1.0 / swift);
            const double wSub = 1.0 / static_cast<double>(swift);
            flat.push_back({respawn + baseFrame / fps, runningIndex++, wMain, false});
            for (int s = 1; s < swift; ++s) {
                const double subFrame = baseFrame + static_cast<double>(s) / static_cast<double>(swift);
                flat.push_back({respawn + subFrame / fps, runningIndex++, wSub, false});
            }
        }
    }

    // Ensure chronological order for time-series evaluation
    std::stable_sort(flat.begin(), flat.end(), [](FlatItem const& a, FlatItem const& b) {
        return a.time < b.time;
    });

    double largestNumericWindow = 0.0;
    for (std::size_t i = 0; i < flat.size(); ++i) {
        flat[i].index = static_cast<std::int64_t>(i + 1);
        if (!flat[i].ignored && flat[i].windowFrames > largestNumericWindow) {
            largestNumericWindow = flat[i].windowFrames;
        }
    }

    out.times.resize(flat.size());
    out.weights.resize(flat.size());

    double prevTime = 0.0;
    std::int64_t prevIndex = 0;

    for (std::size_t m = 0; m < flat.size(); ++m) {
        const double t = std::max(0.0, flat[m].time);
        const std::int64_t idx = flat[m].index;
        out.times[m] = t;

        double effWindowFrames = flat[m].windowFrames;
        if (flat[m].ignored) {
            if (useCps && largestNumericWindow > 0.0) {
                // Per NaNDL spec: ignored '-' with CPS uses (1 + largest numeric window in run)
                effWindowFrames = largestNumericWindow + 1.0;
            } else {
                // Preserves time t & sequential index idx for Nerve/Fatigue/CPS, contributes p = 1
                out.weights[m] = -1.0;
                prevTime = t;
                prevIndex = idx;
                continue;
            }
        }

        const double wSec = effWindowFrames / fps;
        double mult = 1.0;
        if (useNerve) {
            const double nMult = std::exp(-kT * t);
            if (std::isfinite(nMult) && nMult > 0.0) mult *= nMult;
        }
        if (useFatigue) {
            const double fMult = std::exp(-kU * static_cast<double>(idx));
            if (std::isfinite(fMult) && fMult > 0.0) mult *= fMult;
        }
        if (useCps) {
            double dt = 1.0;
            if (idx > prevIndex) {
                const double diff = (t - prevTime) / static_cast<double>(idx - prevIndex);
                dt = (std::isfinite(diff) && diff > 0.0) ? diff : 1.0;
            }
            const double twoCps = std::max(1.0, 2.0 / dt);
            const double cMult = std::pow(4.0 / twoCps, kC);
            if (std::isfinite(cMult) && cMult > 0.0) mult *= cMult;
        }

        const double weight = wSec * kInvTwoSqrtTwo * mult;
        out.weights[m] = (std::isfinite(weight) && weight > 0.0) ? weight : 0.0;
        out.hasActiveWeight = true;

        prevTime = t;
        prevIndex = idx;
    }

    return out;
}

NandlMetrics solvePrepared(
    PreparedInputs const& prep,
    double targetTime,
    double hintL
) {
    NandlMetrics metrics;
    if (prep.times.empty()) return metrics;

    const double target = sanitizePositive(targetTime, 86400.0);
    const double tLast = prep.times.back();

    // Run contains only '-' ignored inputs (all p_j == 1): exact 100% pass for all L
    if (!prep.hasActiveWeight) {
        metrics.lStar = 0.0;
        metrics.passProbability = 1.0;
        metrics.expectedAttemptTime = tLast;
        metrics.expectedClearTime = tLast;
        metrics.valid = true;
        return metrics;
    }

    // If even near-100% pass rate exceeds target time (e.g. tiny user targetTime), return max L
    if (tLast >= target) {
        double p = 0.0;
        double ta = 0.0;
        const double tc = evaluateInternal(
            prep.times, prep.weights, NandlCalculator::kMaxL, false, &p, &ta
        );
        metrics.lStar = NandlCalculator::kMaxL;
        metrics.passProbability = p;
        metrics.expectedAttemptTime = ta;
        metrics.expectedClearTime = tc;
        metrics.valid = true;
        return metrics;
    }

    double L = (std::isfinite(hintL) && hintL > 0.0)
        ? std::clamp(hintL, NandlCalculator::kMinL, NandlCalculator::kMaxL)
        : 1.0;

    // Bracket [lo, hi] such that E[Tc(lo)] > target >= E[Tc(hi)]
    // Recall E[Tc(L)] is strictly decreasing with L.
    double lo = L;
    double hi = L;

    double fLo = evaluateInternal(prep.times, prep.weights, lo, true) - target;
    if (fLo <= 0.0) {
        for (int i = 0; i < 80 && fLo <= 0.0 && lo > NandlCalculator::kMinL; ++i) {
            hi = lo;
            lo = std::max(NandlCalculator::kMinL, lo * 0.5);
            fLo = evaluateInternal(prep.times, prep.weights, lo, true) - target;
            if (lo == NandlCalculator::kMinL) break;
        }
    } else {
        double fHi = fLo;
        for (int i = 0; i < 80 && fHi > 0.0 && hi < NandlCalculator::kMaxL; ++i) {
            lo = hi;
            fLo = fHi;
            hi = std::min(NandlCalculator::kMaxL, hi * 2.0);
            fHi = evaluateInternal(prep.times, prep.weights, hi, true) - target;
            if (hi == NandlCalculator::kMaxL) break;
        }
    }

    if (fLo <= 0.0) {
        L = NandlCalculator::kMinL;
    } else {
        double fHi = evaluateInternal(prep.times, prep.weights, hi, true) - target;
        if (fHi > 0.0) {
            L = hi;
        } else {
            double a = lo;
            double b = hi;
            double fa = fLo;
            double fb = fHi;
            double c = 0.5 * (a + b);

            for (int iter = 0; iter < 64; ++iter) {
                if ((b - a) <= 1e-8 * std::max(1.0, b)) {
                    c = 0.5 * (a + b);
                    break;
                }

                double next = 0.5 * (a + b);
                if (std::isfinite(fa) && std::isfinite(fb) &&
                    std::abs(fa) < 1e30 && std::abs(fb) < 1e30 &&
                    std::abs(fb - fa) > 1e-12) {
                    const double sec = b - fb * (b - a) / (fb - fa);
                    if (std::isfinite(sec) && sec > a + 0.05 * (b - a) && sec < b - 0.05 * (b - a)) {
                        next = sec;
                    }
                }

                const double fn = evaluateInternal(prep.times, prep.weights, next, true) - target;
                c = next;
                if (std::abs(fn) <= 1e-6) {
                    break;
                }
                if (fn > 0.0) {
                    a = next;
                    fa = fn;
                } else {
                    b = next;
                    fb = fn;
                }
            }

            // High-precision polish using exact std::erfc
            for (int iter = 0; iter < 12; ++iter) {
                if ((b - a) <= 1e-10 * std::max(1.0, b)) {
                    c = 0.5 * (a + b);
                    break;
                }
                const double mid = 0.5 * (a + b);
                const double fMid = evaluateInternal(prep.times, prep.weights, mid, false) - target;
                c = mid;
                if (std::abs(fMid) <= 1e-7) break;
                if (fMid > 0.0) a = mid;
                else b = mid;
            }
            L = std::clamp(c, NandlCalculator::kMinL, NandlCalculator::kMaxL);
        }
    }

    double p = 0.0;
    double ta = 0.0;
    const double tc = evaluateInternal(prep.times, prep.weights, L, false, &p, &ta);
    metrics.lStar = L;
    metrics.passProbability = p;
    metrics.expectedAttemptTime = ta;
    metrics.expectedClearTime = tc;
    metrics.valid = true;
    return metrics;
}

} // namespace

double NandlCalculator::evaluateExpectedClearTime(
    std::vector<double> const& times,
    std::vector<double> const& weights,
    double lValue,
    double* outPassProb,
    double* outAttemptTime
) {
    if (lValue <= 0.0) {
        if (outPassProb) *outPassProb = 0.0;
        if (outAttemptTime) *outAttemptTime = 0.0;
        return 1e100;
    }
    return evaluateInternal(times, weights, lValue, false, outPassProb, outAttemptTime);
}

NandlMetrics NandlCalculator::solveModel(
    std::vector<InputTiming> const& inputs,
    NandlParams const& params,
    NandlModel model,
    double hintL
) {
    auto prep = prepareInputs(inputs, params, model);
    return solvePrepared(prep, params.targetTime, hintL);
}

NandlSummary NandlCalculator::solveAll(
    std::vector<InputTiming> const& inputs,
    NandlParams const& params,
    NandlSummary const* hint
) {
    NandlSummary summary;
    summary.base = solveModel(inputs, params, NandlModel::Base, hint ? hint->base.lStar : 1.0);
    summary.nerve = solveModel(inputs, params, NandlModel::Nerve, hint ? hint->nerve.lStar : 1.0);
    summary.fatigue = solveModel(inputs, params, NandlModel::Fatigue, hint ? hint->fatigue.lStar : 1.0);
    summary.cps = solveModel(inputs, params, NandlModel::CPS, hint ? hint->cps.lStar : 1.0);
    summary.nerveFatigue = solveModel(inputs, params, NandlModel::NerveFatigue, hint ? hint->nerveFatigue.lStar : 1.0);
    summary.nerveCps = solveModel(inputs, params, NandlModel::NerveCPS, hint ? hint->nerveCps.lStar : 1.0);
    summary.fatigueCps = solveModel(inputs, params, NandlModel::FatigueCPS, hint ? hint->fatigueCps.lStar : 1.0);
    summary.nfc = solveModel(inputs, params, NandlModel::NFC, hint ? hint->nfc.lStar : 1.0);
    return summary;
}

NandlMetrics const& NandlCalculator::modelMetrics(NandlSummary const& summary, NandlModel model) {
    switch (model) {
        case NandlModel::Base: return summary.base;
        case NandlModel::Nerve: return summary.nerve;
        case NandlModel::Fatigue: return summary.fatigue;
        case NandlModel::CPS: return summary.cps;
        case NandlModel::NerveFatigue: return summary.nerveFatigue;
        case NandlModel::NerveCPS: return summary.nerveCps;
        case NandlModel::FatigueCPS: return summary.fatigueCps;
        case NandlModel::NFC:
        default:
            return summary.nfc;
    }
}

}

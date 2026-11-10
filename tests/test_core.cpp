#include "../src/core/FrameConfig.hpp"
#include "../src/math/NandlCalculator.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace na_frame_counter::core;
using namespace na_frame_counter::math;

namespace {

void expectNear(double actual, double expected, double relTol, char const* label) {
    const double diff = std::abs(actual - expected);
    const double denom = std::max(1.0, std::abs(expected));
    if (diff / denom > relTol) {
        std::cerr << "FAIL [" << label << "]: actual=" << actual
                  << " expected=" << expected << " relDiff=" << (diff / denom) << "\n";
        std::abort();
    }
}

void testSingleInputAnalytical() {
    NandlParams params;
    params.macroFps = 240.0;
    params.respawnTime = 0.0;
    params.targetTime = 86400.0;

    std::vector<InputTiming> inputs = {
        {240.0, 1.0, 1, false}, // t1 = 1.0s, w1 = 1/240s
    };

    const auto base = NandlCalculator::solveModel(inputs, params, NandlModel::Base);
    assert(base.valid);
    // For single click at t1=1s, E[Tc] = t1 / p1 = 86400 => p1 = 1/86400
    expectNear(base.passProbability, 1.0 / 86400.0, 1e-4, "single-input passProbability");
    expectNear(base.expectedClearTime, 86400.0, 1e-4, "single-input expectedClearTime");

    // Analytical check: erf(s / sqrt(2)) = 1/86400 => s = sqrt(2) * erfinv(1/86400)
    // Approx s ~ sqrt(pi/2) / 86400, and L = 2 * s / w1 = 480 * s
    const double approxS = std::sqrt(3.14159265358979323846 / 2.0) / 86400.0;
    const double approxL = 480.0 * approxS;
    expectNear(base.lStar, approxL, 1e-3, "single-input analytical L*");
}

void testAllModelsAndMonotonicity() {
    NandlParams params;
    // 10 clicks at 5 CPS (every 48 frames at 240Hz = 0.2s apart)
    std::vector<InputTiming> inputs;
    for (int i = 1; i <= 10; ++i) {
        inputs.push_back({48.0 * i, (i % 3 == 0) ? 1.0 : 2.0, 1, (i % 2 == 0)});
    }

    const auto summary = NandlCalculator::solveAll(inputs, params);
    assert(summary.base.valid);
    assert(summary.nerve.valid);
    assert(summary.fatigue.valid);
    assert(summary.cps.valid);
    assert(summary.nerveFatigue.valid);
    assert(summary.nerveCps.valid);
    assert(summary.fatigueCps.valid);
    assert(summary.nfc.valid);

    // Every model must converge to target clear time = 86400s
    expectNear(summary.base.expectedClearTime, 86400.0, 1e-3, "multi Base E[Tc]");
    expectNear(summary.nerve.expectedClearTime, 86400.0, 1e-3, "multi Nerve E[Tc]");
    expectNear(summary.fatigue.expectedClearTime, 86400.0, 1e-3, "multi Fatigue E[Tc]");
    expectNear(summary.cps.expectedClearTime, 86400.0, 1e-3, "multi CPS E[Tc]");
    expectNear(summary.nerveFatigue.expectedClearTime, 86400.0, 1e-3, "multi N+F E[Tc]");
    expectNear(summary.nerveCps.expectedClearTime, 86400.0, 1e-3, "multi N+C E[Tc]");
    expectNear(summary.fatigueCps.expectedClearTime, 86400.0, 1e-3, "multi F+C E[Tc]");
    expectNear(summary.nfc.expectedClearTime, 86400.0, 1e-3, "multi NFC E[Tc]");

    // Verify modelMetrics helper maps all 8 models cleanly
    expectNear(NandlCalculator::modelMetrics(summary, NandlModel::NerveFatigue).lStar, summary.nerveFatigue.lStar, 1e-12, "modelMetrics NF");
    expectNear(NandlCalculator::modelMetrics(summary, NandlModel::NerveCPS).lStar, summary.nerveCps.lStar, 1e-12, "modelMetrics NC");
    expectNear(NandlCalculator::modelMetrics(summary, NandlModel::FatigueCPS).lStar, summary.fatigueCps.lStar, 1e-12, "modelMetrics FC");

    // Nerve, Fatigue, and 5-CPS penalty each make the run harder than Base
    assert(summary.nerve.lStar > summary.base.lStar);
    assert(summary.fatigue.lStar > summary.base.lStar);
    assert(summary.cps.lStar > summary.base.lStar);
    assert(summary.nerveFatigue.lStar > summary.nerve.lStar);
    assert(summary.nerveFatigue.lStar > summary.fatigue.lStar);
    assert(summary.nfc.lStar > summary.nerve.lStar);
    assert(summary.nfc.lStar > summary.fatigue.lStar);
    assert(summary.nfc.lStar > summary.cps.lStar);

    // Adding another frame-perfect at the end must increase L*
    auto harder = inputs;
    harder.push_back({528.0, 1.0, 1, false});
    const auto harderSummary = NandlCalculator::solveAll(harder, params, &summary);
    assert(harderSummary.nfc.lStar > summary.nfc.lStar);

    // Slow clicks (0.5 CPS = every 480 frames) yield CPS lambda > 1 => easier than Base
    std::vector<InputTiming> slowInputs = {
        {480.0, 1.0, 1, false},
        {960.0, 1.0, 1, false},
    };
    const auto slow = NandlCalculator::solveAll(slowInputs, params);
    assert(slow.cps.lStar < slow.base.lStar);

    // Swift-click unpacking increases effective difficulty
    std::vector<InputTiming> singleInput = {{240.0, 2.0, 1, false}};
    std::vector<InputTiming> swiftInput = {{240.0, 2.0, 2, false}};
    const auto singleRes = NandlCalculator::solveModel(singleInput, params, NandlModel::Base);
    const auto swiftRes = NandlCalculator::solveModel(swiftInput, params, NandlModel::Base);
    assert(swiftRes.lStar > singleRes.lStar);
}

void testFrameZeroAndIgnoredInputs() {
    NandlParams params;
    params.macroFps = 240.0;
    params.respawnTime = 0.0;
    params.targetTime = 86400.0;

    // Frame-0 first input must NOT blow up CPS via 1e-9 clamp; dt=1.0 => (4/2)^kC = 2^kC multiplier
    std::vector<InputTiming> frameZero = {
        {0.0, 1.0, 1, false, false},
        {240.0, 1.0, 1, false, false},
    };
    const auto fzSummary = NandlCalculator::solveAll(frameZero, params);
    assert(fzSummary.cps.valid);
    assert(fzSummary.nfc.valid);
    // Must stay close to slow 1-CPS range (not blown up 10x+ by 2e9 CPS!)
    assert(fzSummary.cps.lStar < fzSummary.base.lStar);
    expectNear(fzSummary.cps.expectedClearTime, 86400.0, 1e-3, "frame-0 CPS E[Tc]");

    // Only '-' ignored inputs => 100% pass (L* = 0, P = 1)
    std::vector<InputTiming> onlyIgnored = {
        {120.0, 0.0, 1, false, true},
        {240.0, 0.0, 1, false, true},
    };
    const auto onlyIgn = NandlCalculator::solveAll(onlyIgnored, params);
    assert(onlyIgn.base.valid && onlyIgn.nfc.valid);
    expectNear(onlyIgn.base.lStar, 0.0, 1e-12, "only-ignored Base L* == 0");
    expectNear(onlyIgn.base.passProbability, 1.0, 1e-12, "only-ignored Base P == 1");
    expectNear(onlyIgn.nfc.lStar, 0.0, 1e-12, "only-ignored NFC L* == 0");

    // Ignored '-' input before a real 1F input:
    // - Base ignores '-' completely (same L* as the single 1F input alone)
    // - Fatigue counts the '-' input in index i (making Fatigue L* strictly higher than without '-')
    // - CPS treats '-' as (1 + maxWindow = 3F) at high CPS
    std::vector<InputTiming> singleAt480 = {
        {480.0, 2.0, 1, false, false},
    };
    std::vector<InputTiming> withIgnoredBefore = {
        {456.0, 0.0, 1, false, true},
        {480.0, 2.0, 1, false, false},
    };
    const auto sAlone = NandlCalculator::solveAll(singleAt480, params);
    const auto sWithIgn = NandlCalculator::solveAll(withIgnoredBefore, params);
    expectNear(sWithIgn.base.lStar, sAlone.base.lStar, 1e-6, "ignored '-' leaves Base unchanged");
    assert(sWithIgn.fatigue.lStar > sAlone.fatigue.lStar);
    assert(sWithIgn.cps.lStar > sAlone.cps.lStar);
}

void testPitchMapping() {
    ModSettingsSnapshot cfg;
    cfg.minPitch = 0.55f;
    cfg.maxPitch = 1.65f;
    cfg.soundMaxFrames = 12;

    for (auto curve : {PitchCurve::Musical, PitchCurve::Linear, PitchCurve::Semitones}) {
        cfg.pitchCurve = curve;
        expectNear(pitchForWindow(1, cfg), 0.55, 1e-5, "pitch(1) == minPitch");
        expectNear(pitchForWindow(12, cfg), 1.65, 1e-5, "pitch(12) == maxPitch");

        float prev = pitchForWindow(1, cfg);
        for (std::uint64_t f = 2; f <= 12; ++f) {
            const float cur = pitchForWindow(f, cfg);
            // Fewer frames => lower ding; more frames => higher ding
            assert(cur > prev);
            prev = cur;
        }
        // Beyond soundMaxFrames clamps smoothly to maxPitch
        expectNear(pitchForWindow(24, cfg), 1.65, 1e-5, "pitch(24) clamped to maxPitch");
    }
}

void testColorsBucketsAndFormatting() {
    ModSettingsSnapshot cfg;
    cfg.colorPreset = ColorPreset::NangdClassic;
    assert((colorForWindow(1, cfg) == RGBColor{0xFF, 0x00, 0x00}));
    assert((colorForWindow(2, cfg) == RGBColor{0xFF, 0x7F, 0x00}));
    assert((colorForWindow(3, cfg) == RGBColor{0xF6, 0xFF, 0x00}));
    assert((colorForWindow(4, cfg) == RGBColor{0xFF, 0xFF, 0xFF}));
    assert((colorForWindow(5, cfg) == RGBColor{0x00, 0xFF, 0x00}));

    cfg.colorPreset = ColorPreset::Custom;
    cfg.custom1F = {12, 34, 56};
    assert((colorForWindow(1, cfg) == RGBColor{12, 34, 56}));

    for (int preset : {10, 12, 17, 30, 60, 120, 240}) {
        assert(parseMaxWindowPreset(std::to_string(preset)) == preset);
        const auto bottom = bandsForMax(preset, true);
        const auto top = bandsForMax(preset, false);
        assert(bottom.size() == top.size());
        assert(bottom.back().minFrames == 1 && bottom.back().maxFrames == 1);
        assert(top.front().minFrames == 1 && top.front().maxFrames == 1);

        // Verify contiguous coverage of [1..preset] with zero gaps/overlaps
        for (int w = 1; w <= preset; ++w) {
            int hits = 0;
            for (auto const& b : bottom) {
                if (w >= b.minFrames && w <= b.maxFrames) ++hits;
            }
            assert(hits == 1);
        }
    }

    assert(parseNandlDisplayMode("Nerve+Fatigue") == NandlDisplayMode::NerveFatigue);
    assert(parseNandlDisplayMode("Nerve+CPS") == NandlDisplayMode::NerveCPS);
    assert(parseNandlDisplayMode("Fatigue+CPS") == NandlDisplayMode::FatigueCPS);
    assert(parseNandlDisplayMode("All 8 Models") == NandlDisplayMode::AllModels);
    assert(parseNandlDisplayMode("Custom Toggles") == NandlDisplayMode::Custom);

    assert(formatMarkerText(1, false, false, 12, true) == "1");
    assert(formatMarkerText(1, true, false, 12, true) == "R1");
    assert(formatMarkerText(1, true, false, 12, false) == "1");
    assert(formatMarkerText(13, false, true, 12, true) == ">12");
}

} // namespace

int main() {
    testSingleInputAnalytical();
    testAllModelsAndMonotonicity();
    testFrameZeroAndIgnoredInputs();
    testPitchMapping();
    testColorsBucketsAndFormatting();
    std::cout << "All NA Frame Counter core unit tests passed!\n";
    return 0;
}

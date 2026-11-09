#pragma once

#include <cstdint>
#include <vector>

namespace na_frame_counter::math {

struct NandlParams {
    double macroFps = 240.0;
    double respawnTime = 0.0;
    double targetTime = 86400.0;
    double kT = 0.0016520833717346;
    double kU = 0.0002727763242154;
    double kC = 0.2784421686721826;
};

struct InputTiming {
    double frame = 0.0;
    double windowFrames = 1.0;
    int swiftCount = 1;
    bool isRelease = false;
};

enum class NandlModel : int {
    Base = 0,
    Nerve = 1,
    Fatigue = 2,
    CPS = 3,
    NerveFatigue = 4,
    NerveCPS = 5,
    FatigueCPS = 6,
    NFC = 7,
};

struct NandlMetrics {
    double lStar = 0.0;
    double passProbability = 0.0;
    double expectedAttemptTime = 0.0;
    double expectedClearTime = 0.0;
    bool valid = false;
};

struct NandlSummary {
    NandlMetrics base;
    NandlMetrics nerve;
    NandlMetrics fatigue;
    NandlMetrics cps;
    NandlMetrics nerveFatigue;
    NandlMetrics nerveCps;
    NandlMetrics fatigueCps;
    NandlMetrics nfc;
};

class NandlCalculator {
public:
    static constexpr double kMinL = 1e-9;
    static constexpr double kMaxL = 1e12;

    static double evaluateExpectedClearTime(
        std::vector<double> const& times,
        std::vector<double> const& weights,
        double lValue,
        double* outPassProb = nullptr,
        double* outAttemptTime = nullptr
    );

    static NandlMetrics solveModel(
        std::vector<InputTiming> const& inputs,
        NandlParams const& params,
        NandlModel model,
        double hintL = 1.0
    );

    static NandlSummary solveAll(
        std::vector<InputTiming> const& inputs,
        NandlParams const& params,
        NandlSummary const* hint = nullptr
    );
};

}

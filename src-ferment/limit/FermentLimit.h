/* ========================================
 *  FermentLimit - FermentLimit.h
 *
 *  True-peak lookahead brickwall limiter, dual-stage:
 *    - transient bound: sliding minimum of the target gain over a fixed
 *      2 ms window, smoothed by cascaded unequal box averagers of the same
 *      total length — output mathematically cannot exceed the bound, and
 *      the fast stage releases instantly by construction;
 *    - sustain envelope over the transient gain reduction: Attack-knob
 *      hand-off, hold, crest-factor auto-release (Giannoulis/Reiss 2012).
 *      Total reduction = max(transient, sustain): a passing hit rides on
 *      top of the sustain floor and never re-pumps the level.
 *
 *  True peak is a 4x polyphase windowed-sinc *sidechain estimate*
 *  (24 taps/phase — the ITU 12-tap minimum under-reads clipped corners),
 *  group-delay-aligned with the detector, plus a ceiling margin. Full-path
 *  oversampled limiting is deliberately not done (industry consensus:
 *  4x sidechain + margin; see the design notes).
 *
 *  Original implementation from the spec in the design notes; reference
 *  behaviour is the numpy prototype the numpy reference.
 * ======================================== */

#ifndef __FermentLimit_FermentLimit_H
#define __FermentLimit_FermentLimit_H

#ifndef __audioeffect__
#include "../../src/airwin_consolidated_base.h"
#endif

#include <cmath>
#include <vector>

namespace airwinconsolidated::FermentLimit {

enum {
    kParamGain = 0,    // 0..+24 dB drive into the ceiling
    kParamCeiling,     // -24..0 dBFS (dBTP when True Peak is on)
    kParamAttack,      // 10..300 ms — sustain-stage hand-off speed
    kParamRelease,     // 0.25x..4x scale on the crest-factor auto-release
    kParamTransLink,   // 0..100 % transient stereo linking
    kParamTruePeak,    // on/off
    kParamDelta,       // monitor removed signal only
    kParamOutput,      // +/-24 dB post trim
    kNumParameters
};

// Frozen param ABI: downstream pipelines (and the Cuts iOS vendor copy)
// address parameters by index as kParamA.. — indices never reorder, new
// parameters append only.
enum {
    kParamA = kParamGain,
    kParamB = kParamCeiling,
    kParamC = kParamAttack,
    kParamD = kParamRelease,
    kParamE = kParamTransLink,
    kParamF = kParamTruePeak,
    kParamG = kParamDelta,
    kParamH = kParamOutput
};


const int kNumPrograms = 0;
const int kNumInputs = 2;
const int kNumOutputs = 2;
const unsigned long kUniqueId = 'FmLm';

// Fixed internals, voiced by ear across a broad genre set.
const double kAttackWindowMs = 2.0;
const double kHoldMs = 25.0;
const double kTpMarginDb = 0.3;
const int    kTpTapsPerPhase = 24;   // group delay = taps/2 base samples

class FermentLimit : public AirwinConsolidatedBase {
public:
    explicit FermentLimit(audioMasterCallback);
    ~FermentLimit() override = default;

    bool getEffectName(char* name) override;
    bool canDoubleReplacing() override { return true; }
    void processReplacing(float** inputs, float** outputs, VstInt32 sampleFrames) override;
    void processDoubleReplacing(double** inputs, double** outputs, VstInt32 sampleFrames) override;

    void reset() noexcept;

    float getParameter(VstInt32 index) override;
    void  setParameter(VstInt32 index, float value) override;
    void  getParameterLabel(VstInt32 index, char* text) override;
    void  getParameterName(VstInt32 index, char* text) override;
    void  getParameterDisplay(VstInt32 index, char* text) override;
    bool  parameterTextToValue(VstInt32 index, const char* text, float& value) override;
    bool  canConvertParameterTextToValue(VstInt32 index) override;

    // --- Normalised parameter mappings (shared with the JUCE editor) ---
    static double gainDbFromNorm(double v)     { return 24.0 * v; }
    static double ceilingDbFromNorm(double v)  { return -24.0 + 24.0 * v; }
    static double attackMsFromNorm(double v)   { return 10.0 * std::exp(v * 3.4012); }  // 10..300, log
    static double releaseScaleFromNorm(double v) { return 0.25 * std::exp(v * 2.7726); } // 0.25..4, log
    static double trimDbFromNorm(double v)     { return (v - 0.5) * 48.0; }

    // Latency = attack window (min + smoother) + TP group-delay alignment.
    static int latencySamples(double sr)
    {
        return (int)std::lround(kAttackWindowMs * 1e-3 * sr) - 1
               + kTpTapsPerPhase / 2;
    }

    // Gain reduction in the most recent block (dB, >= 0); for the editor.
    double meterGrDb() const noexcept { return grMeterDb; }

private:
    float p[kNumParameters];

    double sm[kNumParameters] = {};
    bool   smoothPrimed = false;

    // --- sliding minimum (monotonic ring deque, O(1) amortised) -----------
    struct SlideMin {
        std::vector<double> v;
        std::vector<long long> ps;
        int head = 0, tail = 0, cap = 0, win = 1;
        long long n = 0;
        void init(int w)
        {
            win = w; cap = w + 1;
            v.assign((size_t)cap, 1.0);
            ps.assign((size_t)cap, 0);
            head = tail = 0; n = 0;
        }
        inline double process(double x) noexcept
        {
            while (tail != head && v[(size_t)((tail + cap - 1) % cap)] >= x)
                tail = (tail + cap - 1) % cap;
            v[(size_t)tail] = x; ps[(size_t)tail] = n;
            tail = (tail + 1) % cap;
            if (ps[(size_t)head] <= n - win) head = (head + 1) % cap;
            ++n;
            return v[(size_t)head];
        }
    };

    // --- moving average primed with a "gain = 1" history --------------------
    struct MovAvg {
        std::vector<double> buf;
        int idx = 0, len = 1;
        double sum = 1.0;
        void init(int n)
        {
            len = n < 1 ? 1 : n;
            buf.assign((size_t)len, 1.0);
            sum = (double)len;
            idx = 0;
        }
        inline double process(double x) noexcept
        {
            sum += x - buf[(size_t)idx];
            buf[(size_t)idx] = x;
            idx = (idx + 1) % len;
            return sum / (double)len;
        }
    };

    struct Lane {
        SlideMin smin;
        MovAvg box1, box2;
        std::vector<double> xRing;    // raw samples for the TP FIR
        std::vector<double> magRing;  // |x| delayed by the TP group delay
        int xIdx = 0, magIdx = 0;
    };
    Lane laneL, laneR;

    std::vector<double> dryL, dryR;   // latency-compensation ring
    int dryIdx = 0;

    double tpPhase[4][kTpTapsPerPhase] = {};

    // crest tracker + sustain envelope (linked)
    double pk2 = 0.0, rms2 = 0.0;
    double susDb = 0.0;
    int holdCount = 0;

    double grMeterDb = 0.0;

    double cachedSr = 0.0;
    int win = 96, latency = 107, holdN = 1200;

    void designTp();

    template <typename T>
    void processT(T** inputs, T** outputs, VstInt32 sampleFrames);
};

} // namespace airwinconsolidated::FermentLimit

#endif

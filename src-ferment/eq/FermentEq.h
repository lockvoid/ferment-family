/*
 * Ferment EQ — Airwindows-style port of the JUCE plugin in
 * `the vendored DSP tree/eq/`.
 *
 * 8 bands × { type, freq, gain, q, on } + global output gain.
 * RBJ biquad cookbook DSP — bit-identical to the JUCE version's `processBlockT`.
 *
 * Param ordering (kept stable so .als params map directly):
 *   band b ∈ [0..7], offset = b * 5
 *     [offset + 0] type (Bell/LoShelf/HiShelf/LoCut/HiCut/Notch — 0..1 → discrete 0..5)
 *     [offset + 1] freq (0..1, log 20 Hz .. 20 kHz)
 *     [offset + 2] gain (0..1, ±15 dB; 0.5 = 0 dB)
 *     [offset + 3] q    (0..1, log 0.1 .. 18; 0.4 ≈ 0.71)
 *     [offset + 4] on   (0..1, threshold 0.5)
 *   [40] output (0..1, ±12 dB; 0.5 = 0 dB)
 */

#ifndef __FermentEq_FermentEq_H
#define __FermentEq_FermentEq_H

#ifndef __audioeffect__
#include "../../src/airwin_consolidated_base.h"
#endif

#include <cmath>
#include <cstdio>
#include <cstring>

namespace airwinconsolidated::FermentEq {

enum BandType { Bell = 0, LowShelf, HighShelf, LowCut, HighCut, Notch, kNumTypes };

constexpr int kNumBands = 8;
constexpr int kParamsPerBand = 5;

enum {
    kBandStride = kParamsPerBand,
    kBandTypeOffset = 0,
    kBandFreqOffset = 1,
    kBandGainOffset = 2,
    kBandQOffset    = 3,
    kBandOnOffset   = 4,
    kParamOutput    = kNumBands * kParamsPerBand,   // 40
    kNumParameters  = kParamOutput + 1              // 41
};

const int kNumPrograms = 0;
const int kNumInputs = 2;
const int kNumOutputs = 2;
const unsigned long kUniqueId = 'FmEq';

struct BiquadState {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double zL1 = 0.0, zL2 = 0.0, zR1 = 0.0, zR2 = 0.0;

    void reset() noexcept { zL1 = zL2 = zR1 = zR2 = 0.0; }
    void setIdentity() noexcept { b0 = 1.0; b1 = 0.0; b2 = 0.0; a1 = 0.0; a2 = 0.0; }

    inline double processL(double x) noexcept {
        const double y = b0 * x + zL1;
        zL1 = b1 * x - a1 * y + zL2;
        zL2 = b2 * x - a2 * y;
        return y;
    }
    inline double processR(double x) noexcept {
        const double y = b0 * x + zR1;
        zR1 = b1 * x - a1 * y + zR2;
        zR2 = b2 * x - a2 * y;
        return y;
    }

    void setFromCookbook(double _b0, double _b1, double _b2,
                         double _a0, double _a1, double _a2) noexcept {
        const double inv = 1.0 / _a0;
        b0 = _b0 * inv; b1 = _b1 * inv; b2 = _b2 * inv;
        a1 = _a1 * inv; a2 = _a2 * inv;
    }
    void setBell(double f, double q, double gDb, double sr) noexcept;
    void setLowShelf(double f, double q, double gDb, double sr) noexcept;
    void setHighShelf(double f, double q, double gDb, double sr) noexcept;
    void setLowCut(double f, double q, double sr) noexcept;
    void setHighCut(double f, double q, double sr) noexcept;
    void setNotch(double f, double q, double sr) noexcept;
};

class FermentEq : public AirwinConsolidatedBase {
public:
    FermentEq(audioMasterCallback);
    ~FermentEq() override = default;

    bool getEffectName(char* name) override;
    bool canDoubleReplacing() override { return true; }
    void processReplacing(float** inputs, float** outputs, VstInt32 sampleFrames) override;
    void processDoubleReplacing(double** inputs, double** outputs, VstInt32 sampleFrames) override;

    float getParameter(VstInt32 index) override;
    void  setParameter(VstInt32 index, float value) override;
    void  getParameterLabel(VstInt32 index, char* text) override;
    void  getParameterName(VstInt32 index, char* text) override;
    void  getParameterDisplay(VstInt32 index, char* text) override;

    static double freqFromNorm(double v) { return 20.0 * std::pow(1000.0, v); }
    static double qFromNorm(double v)    { return 0.1 * std::pow(180.0, v); }
    static double gainFromNorm(double v) { return (v - 0.5) * 30.0; }
    static double outputFromNorm(double v) { return (v - 0.5) * 24.0; }

private:
    float p[kNumParameters];
    BiquadState bands[kNumBands];

    template <typename T>
    void processT(T** inputs, T** outputs, VstInt32 sampleFrames);

    void recomputeCoeffs();
};

} // namespace airwinconsolidated::FermentEq

#endif

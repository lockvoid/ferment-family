/* ========================================
 *  FermentClip - FermentClip.h
 *
 *  Mastering clipper: ADAA morphing-knee waveshaper inside 4x polyphase-IIR
 *  halfband oversampling, with tilt (pre/de-emphasis) clipping, bias for
 *  even harmonics, RMS-matched auto gain and delta monitoring.
 *
 *  Original implementation from the spec in the design notes. The reference
 *  behaviour is the numpy prototype in the numpy reference; voicing was
 *  A/B'd on a broad genre set (the listening notes).
 *
 *  Signal chain:
 *    input trim
 *      -> 2x -> 2x upsample       (polyphase allpass halfband, min-phase:
 *                                  no pre-ring on clipped transients)
 *      -> tilt pre-emphasis       (RBJ shelf pair at the 4x rate)
 *      -> ADAA knee clipper       (+ bias -> even harmonics)
 *      -> tilt de-emphasis        (exact biquad inverse: numerator/denominator
 *                                  swapped — a -gain RBJ shelf is NOT the
 *                                  inverse of the +gain one)
 *      -> 2x -> 2x downsample
 *      -> DC block (bias only) -> auto gain -> mix / delta -> output trim
 *      -> safety hard clip at ceiling (base rate; catches decimation
 *         overshoot — fractions of a dB by design)
 * ======================================== */

#ifndef __FermentClip_FermentClip_H
#define __FermentClip_FermentClip_H

#ifndef __audioeffect__
#include "../../src/airwin_consolidated_base.h"
#endif

#include "../common/Biquad.h"

#include <cmath>

namespace airwinconsolidated::FermentClip {

enum {
    kParamInput = 0,   // +/-24 dB
    kParamCeiling,     // -24 .. 0 dBFS (clip point; safety clip lives here too)
    kParamKnee,        // 0..100%  hard -> soft morph
    kParamTilt,        // 0..100%  pre/de-emphasis clipping amount
    kParamBias,        // -100..+100%  asymmetry -> even harmonics
    kParamMix,         // 0 = dry, 1 = wet
    kParamOutput,      // +/-24 dB
    kParamAutoGain,    // RMS-matched makeup on/off
    kParamDelta,       // monitor removed signal only
    kNumParameters
};

const int kNumPrograms = 0;
const int kNumInputs = 2;
const int kNumOutputs = 2;
const unsigned long kUniqueId = 'FmCp';

// Group delay of the four halfband stages referred to the base rate
// (~4.04 samples per stage at its own high rate: 2.02 + 1.01 + 1.01 + 2.02,
// plus the ADAA half sample at 4x). Non-integer in truth — min-phase IIR —
// so mix/delta null is approximate by design.
const int kLatencySamples = 6;

class FermentClip : public AirwinConsolidatedBase {
public:
    explicit FermentClip(audioMasterCallback);
    ~FermentClip() override = default;

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
    static double trimDbFromNorm(double v)    { return (v - 0.5) * 48.0; }  // +/-24 dB
    static double ceilingDbFromNorm(double v) { return -24.0 + 24.0 * v; }  // -24..0
    static double biasFromNorm(double v)      { return (v - 0.5) * 2.0; }   // -1..+1

    // Peak dB shaved off in the most recent block (>= 0); read by the editor.
    double meterShaveDb() const noexcept { return shaveDb; }

private:
    float p[kNumParameters];

    // Parameter smoothing, charge-style: smoothers advance once per control
    // block, coefficients rebuilt from the smoothed values.
    double sm[kNumParameters] = {};
    bool   smoothPrimed = false;

    // --- polyphase allpass halfband (one 2x stage direction, one channel) ---
    // A(z) = (c + z^-1)/(1 + c z^-1) sections chained per path;
    // H(z) = 0.5 * (A0(z^2) + z^-1 A1(z^2)). Coefficients validated to
    // 0.0 dB passband ripple / -104 dB stopband (see the design notes).
    struct HalfbandPath {
        const double* c = nullptr;
        int n = 0;
        double x1[6] = {}, y1[6] = {};
        inline double process(double x) noexcept
        {
            for (int i = 0; i < n; ++i) {
                const double y = x1[i] + c[i] * (x - y1[i]);
                x1[i] = x; y1[i] = y; x = y;
            }
            return x;
        }
        void reset() noexcept
        {
            for (int i = 0; i < 6; ++i) x1[i] = y1[i] = 0.0;
        }
    };
    struct Upsampler2x {
        HalfbandPath p0, p1;
        inline void process(double x, double& even, double& odd) noexcept
        {
            even = p0.process(x);
            odd  = p1.process(x);
        }
        void reset() noexcept { p0.reset(); p1.reset(); }
    };
    struct Downsampler2x {
        HalfbandPath p0, p1;
        double oddPrev = 0.0;
        inline double process(double x0, double x1) noexcept
        {
            // y[n] = 0.5 (A0(x_even)[n] + A1(x_odd)[n-1]) — the z^-1 of the
            // polyphase decomposition is the previous pair's odd sample.
            const double y = 0.5 * (p0.process(x0) + p1.process(oddPrev));
            oddPrev = x1;
            return y;
        }
        void reset() noexcept { p0.reset(); p1.reset(); oddPrev = 0.0; }
    };
    struct OversamplerChain {
        Upsampler2x upA, upB;      // 1x->2x, 2x->4x
        Downsampler2x downB, downA;
        void reset() noexcept { upA.reset(); upB.reset(); downB.reset(); downA.reset(); }
    };
    OversamplerChain osL, osR;

    // --- tilt (built at the 4x rate) --------------------------------------
    ferment::Biquad tiltHi, tiltLo;        // forward pre-emphasis pair
    ferment::Biquad tiltHiInv, tiltLoInv;  // exact inverses
    double cachedSrOs = 0.0;
    double cachedTilt = -1.0;

    // --- ADAA state (previous normalised input, per channel) ---------------
    double adaaPrevL = 0.0, adaaPrevR = 0.0;
    // Vicanek droop-compensation one-poles (pre/post shaper, per channel).
    double compPreL = 0.0, compPostL = 0.0, compPreR = 0.0, compPostR = 0.0;

    // --- DC blocker (engaged only while bias != 0) --------------------------
    double dcXL = 0.0, dcYL = 0.0, dcXR = 0.0, dcYR = 0.0;

    // --- auto gain: one-pole mean-square trackers ---------------------------
    double msIn = 0.0, msWet = 0.0, autoG = 1.0;

    // --- dry delay ring for mix / delta ------------------------------------
    static const int kDryMask = 15;        // ring of 16 >= kLatencySamples
    double dryL[16] = {}, dryR[16] = {};
    int dryIdx = 0;

    double shaveDb = 0.0;

    void updateTilt(double srOs, double tiltAmt);

    template <typename T>
    void processT(T** inputs, T** outputs, VstInt32 sampleFrames);
};

} // namespace airwinconsolidated::FermentClip

#endif

/* ========================================
 *  FermentCharge - FermentCharge.h
 *
 *  One-knob levelling compressor with saturation and spectral shaping.
 *
 *  Original implementation. The behavioural targets were obtained by
 *  black-box measurement of a licensed reference unit (render audio in,
 *  measure audio out) and fitted to our own models; see
 *  doc/ferment-charge-design.md for the method, the raw data, and the
 *  fit residuals. No third-party code, presets or assets are used.
 *
 *  Signal chain (stage order confirmed by measurement):
 *    input trim
 *      -> detector: HP filter -> sidechain select -> stereo-mode link
 *      -> leveller gain cell   (symmetric; ripple supplies odd harmonics)
 *      -> saturation           (Mild asymmetric / Moderate+Hot symmetric)
 *      -> character            (low shelf + mid bell + high shelf)
 *      -> mix -> output
 * ======================================== */

#ifndef __FermentCharge_FermentCharge_H
#define __FermentCharge_FermentCharge_H

#ifndef __audioeffect__
#include "../../src/airwin_consolidated_base.h"
#endif

#include "../common/Biquad.h"

#include <cmath>
#include <cstdint>

namespace airwinconsolidated::FermentCharge {

enum {
    kParamInput = 0,     // Input Trim   +/-20 dB
    kParamCompression,   // 1 .. 10
    kParamAttack,        // 1 .. 10   (ascending = slower)
    kParamRelease,       // 1 .. 10   (ascending = slower)
    kParamSaturation,    // 1 .. 10
    kParamSatMode,       // Mild / Moderate / Hot
    kParamCharacter,     // 1 .. 10
    kParamCharMode,      // Fat / Warm / Bright
    kParamDetectorHP,    // Off / 100 Hz / 300 Hz
    kParamStereoMode,    // Stereo Link / Dual Mono / M-S
    kParamSidechain,     // external sidechain on/off
    kParamScGain,        // sidechain trim +/-20 dB
    kParamMix,           // 0 = dry, 1 = wet
    kParamOutput,        // +/-20 dB
    kNumParameters
};

enum SatMode    { SatMild = 0, SatModerate, SatHot, kNumSatModes };
enum CharMode   { CharFat = 0, CharWarm, CharBright, kNumCharModes };
enum DetectorHP { HpOff = 0, Hp100, Hp300, kNumDetectorHP };
enum StereoMode { StereoLink = 0, DualMono, MidSide, kNumStereoModes };

const int kNumPrograms = 0;
const int kNumInputs = 4;    // [0,1] = main L/R, [2,3] = sidechain L/R
const int kNumOutputs = 2;
const unsigned long kUniqueId = 'FmCh';

class FermentCharge : public AirwinConsolidatedBase {
public:
    explicit FermentCharge(audioMasterCallback);
    ~FermentCharge() override = default;

    bool getEffectName(char* name) override;
    bool canDoubleReplacing() override { return true; }
    void processReplacing(float** inputs, float** outputs, VstInt32 sampleFrames) override;
    void processDoubleReplacing(double** inputs, double** outputs, VstInt32 sampleFrames) override;

    /* Drop every trace of the audio that has been through here: gain-reduction
     * envelopes, filter memory, and the parameter smoothers (which jump to
     * their targets rather than gliding up from stale values).
     * Call on transport restart and whenever the sample rate changes. */
    void reset() noexcept;

    float getParameter(VstInt32 index) override;
    void  setParameter(VstInt32 index, float value) override;
    void  getParameterLabel(VstInt32 index, char* text) override;
    void  getParameterName(VstInt32 index, char* text) override;
    void  getParameterDisplay(VstInt32 index, char* text) override;
    bool  parameterTextToValue(VstInt32 index, const char* text, float& value) override;
    bool  canConvertParameterTextToValue(VstInt32 index) override;

    // --- Normalised parameter mappings (shared with the JUCE editor) ---
    static double trimDbFromNorm(double v)  { return (v - 0.5) * 40.0; }   // +/-20 dB
    static double knobFromNorm(double v)    { return 1.0 + 9.0 * v; }      // 1 .. 10
    static double normFromKnob(double k)    { return (k - 1.0) / 9.0; }
    static int    modeFromNorm(double v, int n)
    {
        int i = (int)(v * (double)n);
        if (i < 0) i = 0;
        if (i >= n) i = n - 1;
        return i;
    }

    // Gain reduction currently applied (dB, >= 0); for the editor's needle.
    // The greater of the two lanes, so the reading means the same thing in
    // every stereo mode.  Read-only, mirroring FermentLimit/FermentGlue.
    double meterGrDb() const noexcept { return envA > envB ? envA : envB; }

private:
    float p[kNumParameters];

    // --- Parameter smoothing ---------------------------------------------
    // Coefficients are derived from `sm`, never from `p` directly, so that a
    // host writing one value per block produces a ramp rather than a step.
    // Discrete parameters are copied straight across; see kSmoothed in
    // FermentChargeProc.cpp.
    double sm[kNumParameters] = {};
    bool   smoothPrimed = false;   // false => next block snaps sm to p

    // --- Gain cell state -------------------------------------------------
    // Gain reduction envelope in dB (>= 0), one per processing lane. Lane
    // meaning depends on the stereo mode: L/R, or mid/side.
    double envA = 0.0, envB = 0.0;

    // --- Filters ---------------------------------------------------------
    ferment::Biquad detHp;                 // detector high-pass
    ferment::Biquad charLow, charMid, charHigh;
    // NB: the reference appears to have a resting HF lift (~+3 dB at 16 kHz)
    // even at minimum Character, but that measurement is confounded by a
    // known +2.8 dB systematic offset in the sweep analysis. Not modelled
    // until a white-noise probe settles it — see design doc, open questions.

    // Cached coefficient state so we only rebuild filters when needed.
    double detBoost = 1.0;   // set by updateFilters, see FermentChargeProc.cpp
    double cachedSr = 0.0;
    int    cachedHp = -1;
    int    cachedCharMode = -1;
    double cachedCharAmt = -1.0;

    uint32_t fpdL, fpdR;

    void updateFilters(double sr, int hpMode, int charMode, double charAmt);

    template <typename T>
    void processT(T** inputs, T** outputs, VstInt32 sampleFrames);
};

} // namespace airwinconsolidated::FermentCharge

#endif

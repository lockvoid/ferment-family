/*
 * Ferment Utility — the pure-DSP layer behind FermentUtilityProcessor.
 *
 * Routing, gain, phase, stereo field, DC filter, bass mono.
 * Bit-identical DSP to `FermentUtilityProcessor::processBlockT`.
 *
 * Param ordering (kept stable so .als params map directly):
 *   [ 0] gain        (0..1, ±35 dB; 0.5 = 0 dB)
 *   [ 1] mute        (0..1, threshold 0.5)
 *   [ 2] phaseL      (0..1)
 *   [ 3] phaseR      (0..1)
 *   [ 4] chanmode    (0..1, discrete: Stereo/Swap/L/R/Mono)
 *   [ 5] balL        (0..1, mapped to -1..+1)
 *   [ 6] balR        (0..1, mapped to -1..+1)
 *   [ 7] width       (0..1, mapped to 0..4)
 *   [ 8] mssolo      (0..1, discrete: Off/Mid/Side)
 *   [ 9] dc          (0..1)
 *   [10] bassmono    (0..1)
 *   [11] bassmonofreq(0..1, log 40..1000 Hz)
 */

#ifndef __FermentUtility_FermentUtility_H
#define __FermentUtility_FermentUtility_H

#ifndef __audioeffect__
#include "../../src/airwin_consolidated_base.h"
#endif

#include <cmath>
#include <cstdio>
#include <cstring>

namespace airwinconsolidated::FermentUtility {

enum {
    kParamGain = 0,
    kParamMute,
    kParamPhaseL,
    kParamPhaseR,
    kParamChannelMode,
    kParamBalanceL,
    kParamBalanceR,
    kParamWidth,
    kParamMSSolo,
    kParamDC,
    kParamBassMono,
    kParamBassMonoFreq,
    kNumParameters
};

// Frozen param ABI: downstream pipelines (and the Cuts iOS vendor copy)
// address parameters by index as kParamA.. — indices never reorder, new
// parameters append only.
enum {
    kParamA = kParamGain,
    kParamB = kParamMute,
    kParamC = kParamPhaseL,
    kParamD = kParamPhaseR,
    kParamE = kParamChannelMode,
    kParamF = kParamBalanceL,
    kParamG = kParamBalanceR,
    kParamH = kParamWidth,
    kParamI = kParamMSSolo,
    kParamJ = kParamDC,
    kParamK = kParamBassMono,
    kParamL = kParamBassMonoFreq
};


enum ChannelMode { ModeStereo = 0, ModeSwap, ModeLeftOnly, ModeRightOnly, ModeMono, kNumChannelModes };
enum MSSolo      { SoloOff = 0, MidOnly, SideOnly, kNumMSSolo };

const int kNumPrograms = 0;
const int kNumInputs = 2;
const int kNumOutputs = 2;
const unsigned long kUniqueId = 'FmUt';

class FermentUtility : public AirwinConsolidatedBase {
public:
    FermentUtility(audioMasterCallback);
    ~FermentUtility() override = default;

    bool getEffectName(char* name) override;
    bool canDoubleReplacing() override { return true; }
    void processReplacing(float** inputs, float** outputs, VstInt32 sampleFrames) override;
    void processDoubleReplacing(double** inputs, double** outputs, VstInt32 sampleFrames) override;

    float getParameter(VstInt32 index) override;
    void  setParameter(VstInt32 index, float value) override;
    void  getParameterLabel(VstInt32 index, char* text) override;
    void  getParameterName(VstInt32 index, char* text) override;
    void  getParameterDisplay(VstInt32 index, char* text) override;

    static double gainFromNorm(double v)     { return (v - 0.5) * 70.0; }
    static double widthFromNorm(double v)    { return v * 4.0; }
    static double balanceFromNorm(double v)  { return (v - 0.5) * 2.0; }
    static double bassMonoFromNorm(double v) { return 40.0 * std::pow(25.0, v); }

private:
    float p[kNumParameters];

    // DC filter state — one-pole HP at ~5 Hz
    double dcPrevInL = 0.0, dcPrevOutL = 0.0;
    double dcPrevInR = 0.0, dcPrevOutR = 0.0;
    // Bass-mono crossover: 2nd-order Butterworth LP, per channel.
    struct LpState { double z1 = 0.0, z2 = 0.0; };
    LpState bmLpL, bmLpR;
    double bmB0 = 1.0, bmB1 = 0.0, bmB2 = 0.0, bmA1 = 0.0, bmA2 = 0.0;
    double bmLastFreq = -1.0;

    template <typename T>
    void processT(T** inputs, T** outputs, VstInt32 sampleFrames);
};

} // namespace airwinconsolidated::FermentUtility

#endif

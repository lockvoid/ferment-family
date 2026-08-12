/*
 * Ferment Utility implementation. Bit-identical to
 * `FermentUtilityProcessor::processBlockT`.
 */

#include "FermentUtility.h"

#include <algorithm>

namespace airwinconsolidated::FermentUtility {

static inline int discreteFromNorm(float v, int n) {
    return std::clamp(static_cast<int>(v * static_cast<float>(n - 1) + 0.5f), 0, n - 1);
}

FermentUtility::FermentUtility(audioMasterCallback m)
    : AirwinConsolidatedBase(m, kNumPrograms, kNumParameters)
{
    // Defaults mirror the JUCE `buildLayout` defaults.
    p[kParamGain]         = 0.5f;     // 0 dB
    p[kParamMute]         = 0.0f;
    p[kParamPhaseL]       = 0.0f;
    p[kParamPhaseR]       = 0.0f;
    p[kParamChannelMode]  = 0.0f;     // Stereo
    p[kParamBalanceL]     = 0.5f;     // 0
    p[kParamBalanceR]     = 0.5f;
    p[kParamWidth]        = 0.25f;    // width=1.0
    p[kParamMSSolo]       = 0.0f;     // Off
    p[kParamDC]           = 0.0f;
    p[kParamBassMono]     = 0.0f;
    p[kParamBassMonoFreq] = 0.28f;    // ~120 Hz
}

bool FermentUtility::getEffectName(char* name) {
    std::strcpy(name, "FermentUtility");
    return true;
}

float FermentUtility::getParameter(VstInt32 index) {
    if (index < 0 || index >= kNumParameters) return 0.0f;
    return p[index];
}

void FermentUtility::setParameter(VstInt32 index, float value) {
    if (index < 0 || index >= kNumParameters) return;
    p[index] = value;
}

void FermentUtility::getParameterName(VstInt32 index, char* text) {
    static const char* const kNames[kNumParameters] = {
        "Gain", "Mute", "Phase L", "Phase R", "Mode", "Bal L", "Bal R",
        "Width", "M/S", "DC", "BassMono", "BM Freq"
    };
    if (index < 0 || index >= kNumParameters) { text[0] = '\0'; return; }
    std::strcpy(text, kNames[index]);
}

void FermentUtility::getParameterLabel(VstInt32 index, char* text) {
    text[0] = '\0';
    switch (index) {
        case kParamGain:         std::strcpy(text, "dB"); break;
        case kParamWidth:        std::strcpy(text, "%");  break;
        case kParamBassMonoFreq: std::strcpy(text, "Hz"); break;
        default: break;
    }
}

void FermentUtility::getParameterDisplay(VstInt32 index, char* text) {
    if (index < 0 || index >= kNumParameters) { text[0] = '\0'; return; }
    const float v = p[index];
    switch (index) {
        case kParamGain:        std::snprintf(text, kVstMaxParamStrLen, "%+.1f", gainFromNorm(v)); break;
        case kParamMute:        std::strcpy(text, v >= 0.5f ? "On" : "Off"); break;
        case kParamPhaseL:
        case kParamPhaseR:      std::strcpy(text, v >= 0.5f ? "Inv" : "Norm"); break;
        case kParamChannelMode: {
            static const char* const kNames[kNumChannelModes] = {"Stereo","Swap","L","R","Mono"};
            std::strcpy(text, kNames[discreteFromNorm(v, kNumChannelModes)]);
            break;
        }
        case kParamBalanceL:
        case kParamBalanceR:    std::snprintf(text, kVstMaxParamStrLen, "%+.2f", balanceFromNorm(v)); break;
        case kParamWidth:       std::snprintf(text, kVstMaxParamStrLen, "%.0f", widthFromNorm(v) * 100.0); break;
        case kParamMSSolo: {
            static const char* const kNames[kNumMSSolo] = {"Off","Mid","Side"};
            std::strcpy(text, kNames[discreteFromNorm(v, kNumMSSolo)]);
            break;
        }
        case kParamDC:           std::strcpy(text, v >= 0.5f ? "On" : "Off"); break;
        case kParamBassMono:     std::strcpy(text, v >= 0.5f ? "On" : "Off"); break;
        case kParamBassMonoFreq: std::snprintf(text, kVstMaxParamStrLen, "%.0f", bassMonoFromNorm(v)); break;
        default: text[0] = '\0';
    }
}

template <typename T>
void FermentUtility::processT(T** inputs, T** outputs, VstInt32 sampleFrames) {
    // The member, not getSampleRate(): the accessor asserts on a degenerate
    // rate in Debug builds, and the DC blocker and bass-mono crossover both
    // divide by sr.
    double sr = static_cast<double>(sampleRate);
    if (sr < 8000.0) sr = 48000.0;

    const double gainDb  = gainFromNorm(p[kParamGain]);
    const double gainLin = std::pow(10.0, gainDb / 20.0);
    const bool   mute    = p[kParamMute]   >= 0.5f;
    const bool   phaseL  = p[kParamPhaseL] >= 0.5f;
    const bool   phaseR  = p[kParamPhaseR] >= 0.5f;
    const int    mode    = discreteFromNorm(p[kParamChannelMode], kNumChannelModes);
    const double balL    = balanceFromNorm(p[kParamBalanceL]);
    const double balR    = balanceFromNorm(p[kParamBalanceR]);
    const double width   = widthFromNorm(p[kParamWidth]);
    const int    ms      = discreteFromNorm(p[kParamMSSolo], kNumMSSolo);
    const bool   dcOn    = p[kParamDC]       >= 0.5f;
    const bool   bmOn    = p[kParamBassMono] >= 0.5f;
    const double bmFreq  = bassMonoFromNorm(p[kParamBassMonoFreq]);

    const double balLScale = (balL >= 0.0) ? 1.0 : (1.0 + balL);
    const double balRScale = (balR >= 0.0) ? 1.0 : (1.0 + balR);

    const double dcR = 1.0 - (2.0 * M_PI * 5.0 / sr);

    // Bass-mono LP coefficients (Butterworth Q=0.707), recompute on freq change.
    if (bmOn && std::fabs(bmFreq - bmLastFreq) > 0.01) {
        const double f = std::clamp(bmFreq, 20.0, sr * 0.49);
        const double w = 2.0 * M_PI * f / sr;
        const double cosw = std::cos(w);
        const double sinw = std::sin(w);
        const double alpha = sinw / (2.0 * 0.707);
        const double a0 = 1.0 + alpha;
        bmB0 = ((1.0 - cosw) * 0.5) / a0;
        bmB1 = (1.0 - cosw) / a0;
        bmB2 = bmB0;
        bmA1 = (-2.0 * cosw) / a0;
        bmA2 = (1.0 - alpha) / a0;
        bmLastFreq = bmFreq;
    }

    T* L = outputs[0];
    T* R = outputs[1];
    const T* InL = inputs[0];
    const T* InR = inputs[1];

    for (VstInt32 n = 0; n < sampleFrames; ++n) {
        double l = static_cast<double>(InL[n]);
        double r = static_cast<double>(InR[n]);

        if (phaseL) l = -l;
        if (phaseR) r = -r;

        l *= gainLin;
        r *= gainLin;

        switch (mode) {
            case ModeSwap:      std::swap(l, r); break;
            case ModeLeftOnly:  r = l; break;
            case ModeRightOnly: l = r; break;
            case ModeMono:      { const double m = (l + r) * 0.5; l = m; r = m; break; }
            case ModeStereo:
            default:            break;
        }

        l *= balLScale;
        r *= balRScale;

        // Width + M/S solo via M/S encode
        {
            double midS  = (l + r) * 0.5;
            double sideS = (l - r) * 0.5;
            sideS *= width;
            if      (ms == MidOnly)  sideS = 0.0;
            else if (ms == SideOnly) midS  = 0.0;
            l = midS + sideS;
            r = midS - sideS;
        }

        if (bmOn) {
            const double lowL = bmB0 * l + bmLpL.z1;
            bmLpL.z1 = bmB1 * l - bmA1 * lowL + bmLpL.z2;
            bmLpL.z2 = bmB2 * l - bmA2 * lowL;

            const double lowR = bmB0 * r + bmLpR.z1;
            bmLpR.z1 = bmB1 * r - bmA1 * lowR + bmLpR.z2;
            bmLpR.z2 = bmB2 * r - bmA2 * lowR;

            const double highL = l - lowL;
            const double highR = r - lowR;
            const double lowMono = (lowL + lowR) * 0.5;
            l = lowMono + highL;
            r = lowMono + highR;
        }

        if (dcOn) {
            const double outL = l - dcPrevInL + dcR * dcPrevOutL;
            dcPrevInL = l;    dcPrevOutL = outL;   l = outL;
            const double outR = r - dcPrevInR + dcR * dcPrevOutR;
            dcPrevInR = r;    dcPrevOutR = outR;   r = outR;
        }

        if (mute) { l = 0.0; r = 0.0; }

        L[n] = static_cast<T>(l);
        R[n] = static_cast<T>(r);
    }
}

void FermentUtility::processReplacing(float** inputs, float** outputs, VstInt32 sampleFrames) {
    processT(inputs, outputs, sampleFrames);
}
void FermentUtility::processDoubleReplacing(double** inputs, double** outputs, VstInt32 sampleFrames) {
    processT(inputs, outputs, sampleFrames);
}

} // namespace airwinconsolidated::FermentUtility

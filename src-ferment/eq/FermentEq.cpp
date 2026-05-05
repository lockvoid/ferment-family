/*
 * Ferment EQ implementation. Mirrors `FermentEqProcessor::processBlockT`
 * in `the vendored DSP tree/eq/FermentEqProcessor.cpp`.
 */

#include "FermentEq.h"

#include <algorithm>

namespace airwinconsolidated::FermentEq {

// ---------- Biquad coefficient setters (RBJ cookbook, identical to common/Biquad.h) ----------

void BiquadState::setBell(double freq, double q, double gainDb, double sr) noexcept {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = 2.0 * M_PI * freq / sr;
    const double cosw = std::cos(w);
    const double sinw = std::sin(w);
    const double alpha = sinw / (2.0 * q);
    setFromCookbook(
        1.0 + alpha * A,    -2.0 * cosw,    1.0 - alpha * A,
        1.0 + alpha / A,    -2.0 * cosw,    1.0 - alpha / A);
}

void BiquadState::setLowShelf(double freq, double q, double gainDb, double sr) noexcept {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = 2.0 * M_PI * freq / sr;
    const double cosw = std::cos(w);
    const double sinw = std::sin(w);
    const double alpha = sinw / (2.0 * q);
    const double twoSqrtAalpha = 2.0 * std::sqrt(A) * alpha;
    setFromCookbook(
        A * ((A + 1) - (A - 1) * cosw + twoSqrtAalpha),
        2 * A * ((A - 1) - (A + 1) * cosw),
        A * ((A + 1) - (A - 1) * cosw - twoSqrtAalpha),
        (A + 1) + (A - 1) * cosw + twoSqrtAalpha,
        -2 * ((A - 1) + (A + 1) * cosw),
        (A + 1) + (A - 1) * cosw - twoSqrtAalpha);
}

void BiquadState::setHighShelf(double freq, double q, double gainDb, double sr) noexcept {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = 2.0 * M_PI * freq / sr;
    const double cosw = std::cos(w);
    const double sinw = std::sin(w);
    const double alpha = sinw / (2.0 * q);
    const double twoSqrtAalpha = 2.0 * std::sqrt(A) * alpha;
    setFromCookbook(
        A * ((A + 1) + (A - 1) * cosw + twoSqrtAalpha),
        -2 * A * ((A - 1) + (A + 1) * cosw),
        A * ((A + 1) + (A - 1) * cosw - twoSqrtAalpha),
        (A + 1) - (A - 1) * cosw + twoSqrtAalpha,
        2 * ((A - 1) - (A + 1) * cosw),
        (A + 1) - (A - 1) * cosw - twoSqrtAalpha);
}

void BiquadState::setLowCut(double freq, double q, double sr) noexcept {
    const double w = 2.0 * M_PI * freq / sr;
    const double cosw = std::cos(w);
    const double sinw = std::sin(w);
    const double alpha = sinw / (2.0 * q);
    setFromCookbook(
        (1.0 + cosw) * 0.5, -(1.0 + cosw),  (1.0 + cosw) * 0.5,
        1.0 + alpha,        -2.0 * cosw,    1.0 - alpha);
}

void BiquadState::setHighCut(double freq, double q, double sr) noexcept {
    const double w = 2.0 * M_PI * freq / sr;
    const double cosw = std::cos(w);
    const double sinw = std::sin(w);
    const double alpha = sinw / (2.0 * q);
    setFromCookbook(
        (1.0 - cosw) * 0.5, 1.0 - cosw,     (1.0 - cosw) * 0.5,
        1.0 + alpha,        -2.0 * cosw,    1.0 - alpha);
}

void BiquadState::setNotch(double freq, double q, double sr) noexcept {
    const double w = 2.0 * M_PI * freq / sr;
    const double cosw = std::cos(w);
    const double sinw = std::sin(w);
    const double alpha = sinw / (2.0 * q);
    setFromCookbook(
        1.0,                -2.0 * cosw,    1.0,
        1.0 + alpha,        -2.0 * cosw,    1.0 - alpha);
}

// ---------- FermentEq ----------

FermentEq::FermentEq(audioMasterCallback m)
    : AirwinConsolidatedBase(m, kNumPrograms, kNumParameters)
{
    // Defaults mirror the JUCE `buildLayout` defaults exactly.
    // Band layout: LoCut, LoShelf, Bell, Bell, Bell, Bell, HiShelf, HiCut.
    static constexpr int defaultType[kNumBands] = {
        LowCut, LowShelf, Bell, Bell, Bell, Bell, HighShelf, HighCut
    };
    static constexpr double defaultHz[kNumBands] = {
        40, 120, 350, 900, 2500, 6000, 10000, 16000
    };

    for (int b = 0; b < kNumBands; ++b) {
        const int off = b * kBandStride;
        // 'type' encoded as float in [0..1]; map to discrete via floor((kNumTypes) * v).
        // Default: store discrete index normalized so round-trip is stable.
        p[off + kBandTypeOffset] = static_cast<float>(defaultType[b]) / static_cast<float>(kNumTypes - 1);
        // freq stored as normalised [0..1]: 0 = 20 Hz, 1 = 20 kHz (log).
        p[off + kBandFreqOffset] = static_cast<float>(std::log(defaultHz[b] / 20.0) / std::log(1000.0));
        p[off + kBandGainOffset] = 0.5f;        // 0 dB
        p[off + kBandQOffset]    = 0.4f;        // ~0.71
        // Default enable: bands 3..6 (= index 2..5) on, others off.
        p[off + kBandOnOffset]   = (b >= 2 && b <= 5) ? 1.0f : 0.0f;
    }
    p[kParamOutput] = 0.5f;   // 0 dB

    for (auto& bq : bands) bq.setIdentity();
}

bool FermentEq::getEffectName(char* name) {
    std::strcpy(name, "FermentEq");
    return true;
}

float FermentEq::getParameter(VstInt32 index) {
    if (index < 0 || index >= kNumParameters) return 0.0f;
    return p[index];
}

void FermentEq::setParameter(VstInt32 index, float value) {
    if (index < 0 || index >= kNumParameters) return;
    p[index] = value;
}

void FermentEq::getParameterName(VstInt32 index, char* text) {
    if (index == kParamOutput) { std::strcpy(text, "Output"); return; }
    if (index < 0 || index >= kNumParameters) { text[0] = '\0'; return; }
    const int b = index / kBandStride;
    const int sub = index % kBandStride;
    static const char* const subNames[kParamsPerBand] = { "Type", "Freq", "Gain", "Q", "On" };
    std::snprintf(text, kVstMaxParamStrLen, "B%d %s", b + 1, subNames[sub]);
}

void FermentEq::getParameterLabel(VstInt32 index, char* text) {
    if (index < 0 || index >= kNumParameters) { text[0] = '\0'; return; }
    if (index == kParamOutput) { std::strcpy(text, "dB"); return; }
    const int sub = index % kBandStride;
    switch (sub) {
        case kBandFreqOffset: std::strcpy(text, "Hz"); break;
        case kBandGainOffset: std::strcpy(text, "dB"); break;
        case kBandQOffset:    std::strcpy(text, "Q");  break;
        default:              text[0] = '\0';          break;
    }
}

void FermentEq::getParameterDisplay(VstInt32 index, char* text) {
    if (index < 0 || index >= kNumParameters) { text[0] = '\0'; return; }
    if (index == kParamOutput) {
        std::snprintf(text, kVstMaxParamStrLen, "%+.1f", outputFromNorm(p[index]));
        return;
    }
    const int sub = index % kBandStride;
    const float v = p[index];
    switch (sub) {
        case kBandTypeOffset: {
            static const char* const kNames[kNumTypes] = {"Bell","LoShelf","HiShelf","LoCut","HiCut","Notch"};
            int t = std::clamp(static_cast<int>(v * static_cast<float>(kNumTypes - 1) + 0.5f), 0, kNumTypes - 1);
            std::strcpy(text, kNames[t]);
            break;
        }
        case kBandFreqOffset: std::snprintf(text, kVstMaxParamStrLen, "%.0f", freqFromNorm(v)); break;
        case kBandGainOffset: std::snprintf(text, kVstMaxParamStrLen, "%+.1f", gainFromNorm(v)); break;
        case kBandQOffset:    std::snprintf(text, kVstMaxParamStrLen, "%.2f", qFromNorm(v)); break;
        case kBandOnOffset:   std::strcpy(text, v >= 0.5f ? "On" : "Off"); break;
    }
}

void FermentEq::recomputeCoeffs() {
    const double sr = static_cast<double>(getSampleRate());
    for (int b = 0; b < kNumBands; ++b) {
        const int off = b * kBandStride;
        const bool on = p[off + kBandOnOffset] >= 0.5f;
        if (!on) { bands[b].setIdentity(); continue; }

        const float vType = p[off + kBandTypeOffset];
        const int type = std::clamp(
            static_cast<int>(vType * static_cast<float>(kNumTypes - 1) + 0.5f),
            0, kNumTypes - 1);

        const double freq = std::clamp(freqFromNorm(p[off + kBandFreqOffset]),
                                       20.0, sr * 0.49);
        const double q    = qFromNorm(p[off + kBandQOffset]);
        const double gDb  = gainFromNorm(p[off + kBandGainOffset]);

        switch (type) {
            case Bell:      bands[b].setBell(freq, q, gDb, sr); break;
            case LowShelf:  bands[b].setLowShelf(freq, q, gDb, sr); break;
            case HighShelf: bands[b].setHighShelf(freq, q, gDb, sr); break;
            case LowCut:    bands[b].setLowCut(freq, q, sr); break;
            case HighCut:   bands[b].setHighCut(freq, q, sr); break;
            case Notch:     bands[b].setNotch(freq, q, sr); break;
            default:        bands[b].setIdentity(); break;
        }
    }
}

template <typename T>
void FermentEq::processT(T** inputs, T** outputs, VstInt32 sampleFrames) {
    recomputeCoeffs();
    const double outLin = std::pow(10.0, outputFromNorm(p[kParamOutput]) / 20.0);

    T* L = outputs[0];
    T* R = outputs[1];
    const T* InL = inputs[0];
    const T* InR = inputs[1];

    for (VstInt32 n = 0; n < sampleFrames; ++n) {
        double l = static_cast<double>(InL[n]);
        double r = static_cast<double>(InR[n]);
        for (auto& bq : bands) {
            l = bq.processL(l);
            r = bq.processR(r);
        }
        l *= outLin;
        r *= outLin;
        L[n] = static_cast<T>(l);
        R[n] = static_cast<T>(r);
    }
}

void FermentEq::processReplacing(float** inputs, float** outputs, VstInt32 sampleFrames) {
    processT(inputs, outputs, sampleFrames);
}
void FermentEq::processDoubleReplacing(double** inputs, double** outputs, VstInt32 sampleFrames) {
    processT(inputs, outputs, sampleFrames);
}

} // namespace airwinconsolidated::FermentEq

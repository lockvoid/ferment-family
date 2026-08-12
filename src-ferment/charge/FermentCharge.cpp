/* ========================================
 *  FermentCharge - FermentCharge.cpp
 *  Parameter plumbing and display formatting.
 * ======================================== */

#include "FermentCharge.h"

#include <cstdio>
#include <cstring>

namespace airwinconsolidated::FermentCharge {

AudioEffect* createEffectInstance(audioMasterCallback audioMaster)
{
    return new FermentCharge(audioMaster);
}

FermentCharge::FermentCharge(audioMasterCallback audioMaster)
    : AirwinConsolidatedBase(audioMaster, kNumPrograms, kNumParameters)
{
    p[kParamInput]       = 0.5f;   //   0 dB
    p[kParamCompression] = 0.0f;   //   knob 1.00
    p[kParamAttack]      = 0.3667f;//   knob 4.30 — reference default
    p[kParamRelease]     = 0.4444f;//   knob 5.00 — reference default
    p[kParamSaturation]  = 0.0f;   //   knob 1.00
    p[kParamSatMode]     = 0.0f;   //   Mild
    p[kParamCharacter]   = 0.0f;   //   knob 1.00
    p[kParamCharMode]    = 0.0f;   //   Fat
    p[kParamDetectorHP]  = 0.0f;   //   Off
    p[kParamStereoMode]  = 0.0f;   //   Stereo Link
    p[kParamSidechain]   = 0.0f;   //   off
    p[kParamScGain]      = 0.5f;   //   0 dB
    p[kParamMix]         = 1.0f;   //   full wet
    p[kParamOutput]      = 0.5f;   //   0 dB

    fpdL = 1; while (fpdL < 16386) fpdL = rand() * UINT32_MAX;
    fpdR = 1; while (fpdR < 16386) fpdR = rand() * UINT32_MAX;

    setNumInputs(kNumInputs);
    setNumOutputs(kNumOutputs);
    setUniqueID(kUniqueId);
    canProcessReplacing();
    canDoubleReplacing();
}

bool FermentCharge::getEffectName(char* name)
{
    vst_strncpy(name, "FermentCharge", kVstMaxProductStrLen);
    return true;
}

void FermentCharge::setParameter(VstInt32 index, float value)
{
    if (index < 0 || index >= kNumParameters) return;
    // Every parameter is normalised 0..1. Clamp at the boundary rather than
    // trusting the host: an out-of-range value propagates into dB conversions
    // and overflows to infinity in the audio path. The !(v >= 0) form also
    // catches NaN, which a plain std::max would pass straight through.
    if (!(value >= 0.0f)) value = 0.0f;
    if (value > 1.0f)     value = 1.0f;
    p[index] = value;
}

float FermentCharge::getParameter(VstInt32 index)
{
    if (index < 0 || index >= kNumParameters) return 0.0f;
    return p[index];
}

void FermentCharge::getParameterName(VstInt32 index, char* text)
{
    switch (index) {
        case kParamInput:       vst_strncpy(text, "Input",   kVstMaxParamStrLen); break;
        case kParamCompression: vst_strncpy(text, "Compres", kVstMaxParamStrLen); break;
        case kParamAttack:      vst_strncpy(text, "Attack",  kVstMaxParamStrLen); break;
        case kParamRelease:     vst_strncpy(text, "Release", kVstMaxParamStrLen); break;
        case kParamSaturation:  vst_strncpy(text, "Sat",     kVstMaxParamStrLen); break;
        case kParamSatMode:     vst_strncpy(text, "SatMode", kVstMaxParamStrLen); break;
        case kParamCharacter:   vst_strncpy(text, "Charctr", kVstMaxParamStrLen); break;
        case kParamCharMode:    vst_strncpy(text, "ChrMode", kVstMaxParamStrLen); break;
        case kParamDetectorHP:  vst_strncpy(text, "DetHP",   kVstMaxParamStrLen); break;
        case kParamStereoMode:  vst_strncpy(text, "Stereo",  kVstMaxParamStrLen); break;
        case kParamSidechain:   vst_strncpy(text, "SideChn", kVstMaxParamStrLen); break;
        case kParamScGain:      vst_strncpy(text, "SCGain",  kVstMaxParamStrLen); break;
        case kParamMix:         vst_strncpy(text, "Mix",     kVstMaxParamStrLen); break;
        case kParamOutput:      vst_strncpy(text, "Output",  kVstMaxParamStrLen); break;
        default: break;
    }
}

void FermentCharge::getParameterDisplay(VstInt32 index, char* text)
{
    static const char* kSat[]    = { "Mild", "Moderate", "Hot" };
    static const char* kChar[]   = { "Fat", "Warm", "Bright" };
    static const char* kHp[]     = { "Off", "100 Hz", "300 Hz" };
    static const char* kStereo[] = { "Link", "Dual Mono", "M/S" };

    const double v = (double)p[index];
    switch (index) {
        case kParamInput:
        case kParamScGain:
        case kParamOutput:
            snprintf(text, kVstMaxParamStrLen, "%.1f", trimDbFromNorm(v));
            break;
        case kParamCompression:
        case kParamAttack:
        case kParamRelease:
        case kParamSaturation:
        case kParamCharacter:
            snprintf(text, kVstMaxParamStrLen, "%.2f", knobFromNorm(v));
            break;
        case kParamSatMode:
            vst_strncpy(text, kSat[modeFromNorm(v, kNumSatModes)], kVstMaxParamStrLen);
            break;
        case kParamCharMode:
            vst_strncpy(text, kChar[modeFromNorm(v, kNumCharModes)], kVstMaxParamStrLen);
            break;
        case kParamDetectorHP:
            vst_strncpy(text, kHp[modeFromNorm(v, kNumDetectorHP)], kVstMaxParamStrLen);
            break;
        case kParamStereoMode:
            vst_strncpy(text, kStereo[modeFromNorm(v, kNumStereoModes)], kVstMaxParamStrLen);
            break;
        case kParamSidechain:
            vst_strncpy(text, v >= 0.5 ? "On" : "Off", kVstMaxParamStrLen);
            break;
        case kParamMix:
            snprintf(text, kVstMaxParamStrLen, "%.0f%%", v * 100.0);
            break;
        default: break;
    }
}

void FermentCharge::getParameterLabel(VstInt32 index, char* text)
{
    switch (index) {
        case kParamInput:
        case kParamScGain:
        case kParamOutput:
            vst_strncpy(text, "dB", kVstMaxParamStrLen);
            break;
        default:
            vst_strncpy(text, "", kVstMaxParamStrLen);
            break;
    }
}

bool FermentCharge::canConvertParameterTextToValue(VstInt32 index)
{
    switch (index) {
        case kParamInput:
        case kParamScGain:
        case kParamOutput:
        case kParamCompression:
        case kParamAttack:
        case kParamRelease:
        case kParamSaturation:
        case kParamCharacter:
        case kParamMix:
            return true;
        default:
            return false;
    }
}

bool FermentCharge::parameterTextToValue(VstInt32 index, const char* text, float& value)
{
    float parsed = 0.0f;
    if (!string2float(text, parsed)) return false;

    switch (index) {
        case kParamInput:
        case kParamScGain:
        case kParamOutput:
            value = (float)(parsed / 40.0 + 0.5);
            return true;
        case kParamCompression:
        case kParamAttack:
        case kParamRelease:
        case kParamSaturation:
        case kParamCharacter:
            value = (float)normFromKnob(parsed);
            return true;
        case kParamMix:
            value = (float)(parsed / 100.0);
            return true;
        default:
            return false;
    }
}

} // namespace airwinconsolidated::FermentCharge

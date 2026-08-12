/* ========================================
 *  FermentClip - FermentClip.cpp
 *  Parameter plumbing and display formatting.
 * ======================================== */

#include "FermentClip.h"

#include <cstdio>
#include <cstring>

namespace airwinconsolidated::FermentClip {

AudioEffect* createEffectInstance(audioMasterCallback audioMaster)
{
    return new FermentClip(audioMaster);
}

FermentClip::FermentClip(audioMasterCallback audioMaster)
    : AirwinConsolidatedBase(audioMaster, kNumPrograms, kNumParameters)
{
    p[kParamInput]    = 0.5f;    //  0 dB
    p[kParamCeiling]  = 1.0f;    //  0 dBFS
    p[kParamKnee]     = 0.35f;   //  prototype voicing
    p[kParamTilt]     = 0.5f;    //  prototype voicing
    p[kParamBias]     = 0.5f;    //  0 (symmetric)
    p[kParamMix]      = 1.0f;    //  full wet
    p[kParamOutput]   = 0.5f;    //  0 dB
    p[kParamAutoGain] = 0.0f;    //  off
    p[kParamDelta]    = 0.0f;    //  off

    setNumInputs(kNumInputs);
    setNumOutputs(kNumOutputs);
    setUniqueID(kUniqueId);
    canProcessReplacing();
    canDoubleReplacing();
    reset();
}

bool FermentClip::getEffectName(char* name)
{
    vst_strncpy(name, "FermentClip", kVstMaxProductStrLen);
    return true;
}

void FermentClip::setParameter(VstInt32 index, float value)
{
    if (index < 0 || index >= kNumParameters) return;
    // Clamp instead of trusting the host; !(v >= 0) also catches NaN.
    if (!(value >= 0.0f)) value = 0.0f;
    if (value > 1.0f)     value = 1.0f;
    p[index] = value;
}

float FermentClip::getParameter(VstInt32 index)
{
    if (index < 0 || index >= kNumParameters) return 0.0f;
    return p[index];
}

void FermentClip::getParameterName(VstInt32 index, char* text)
{
    switch (index) {
        case kParamInput:    vst_strncpy(text, "Input",   kVstMaxParamStrLen); break;
        case kParamCeiling:  vst_strncpy(text, "Ceiling", kVstMaxParamStrLen); break;
        case kParamKnee:     vst_strncpy(text, "Knee",    kVstMaxParamStrLen); break;
        case kParamTilt:     vst_strncpy(text, "Tilt",    kVstMaxParamStrLen); break;
        case kParamBias:     vst_strncpy(text, "Bias",    kVstMaxParamStrLen); break;
        case kParamMix:      vst_strncpy(text, "Mix",     kVstMaxParamStrLen); break;
        case kParamOutput:   vst_strncpy(text, "Output",  kVstMaxParamStrLen); break;
        case kParamAutoGain: vst_strncpy(text, "AutoGn",  kVstMaxParamStrLen); break;
        case kParamDelta:    vst_strncpy(text, "Delta",   kVstMaxParamStrLen); break;
        default: break;
    }
}

void FermentClip::getParameterDisplay(VstInt32 index, char* text)
{
    const double v = (double)p[index];
    switch (index) {
        case kParamInput:
        case kParamOutput:
            snprintf(text, kVstMaxParamStrLen, "%.1f", trimDbFromNorm(v));
            break;
        case kParamCeiling:
            snprintf(text, kVstMaxParamStrLen, "%.1f", ceilingDbFromNorm(v));
            break;
        case kParamKnee:
        case kParamTilt:
        case kParamMix:
            snprintf(text, kVstMaxParamStrLen, "%.0f%%", v * 100.0);
            break;
        case kParamBias:
            snprintf(text, kVstMaxParamStrLen, "%+.0f%%", biasFromNorm(v) * 100.0);
            break;
        case kParamAutoGain:
        case kParamDelta:
            vst_strncpy(text, v >= 0.5 ? "On" : "Off", kVstMaxParamStrLen);
            break;
        default: break;
    }
}

void FermentClip::getParameterLabel(VstInt32 index, char* text)
{
    switch (index) {
        case kParamInput:
        case kParamOutput:
        case kParamCeiling:
            vst_strncpy(text, "dB", kVstMaxParamStrLen);
            break;
        default:
            vst_strncpy(text, "", kVstMaxParamStrLen);
            break;
    }
}

bool FermentClip::canConvertParameterTextToValue(VstInt32 index)
{
    switch (index) {
        case kParamAutoGain:
        case kParamDelta:
            return false;
        default:
            return true;
    }
}

bool FermentClip::parameterTextToValue(VstInt32 index, const char* text, float& value)
{
    float parsed = 0.0f;
    if (!string2float(text, parsed)) return false;

    switch (index) {
        case kParamInput:
        case kParamOutput:
            value = (float)(parsed / 48.0 + 0.5);
            return true;
        case kParamCeiling:
            value = (float)((parsed + 24.0) / 24.0);
            return true;
        case kParamKnee:
        case kParamTilt:
        case kParamMix:
            value = (float)(parsed / 100.0);
            return true;
        case kParamBias:
            value = (float)(parsed / 200.0 + 0.5);
            return true;
        default:
            return false;
    }
}

} // namespace airwinconsolidated::FermentClip

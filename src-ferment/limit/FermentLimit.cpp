/* ========================================
 *  FermentLimit - FermentLimit.cpp
 *  Parameter plumbing and display formatting.
 * ======================================== */

#include "FermentLimit.h"

#include <cstdio>
#include <cstring>

namespace airwinconsolidated::FermentLimit {

AudioEffect* createEffectInstance(audioMasterCallback audioMaster)
{
    return new FermentLimit(audioMaster);
}

FermentLimit::FermentLimit(audioMasterCallback audioMaster)
    : AirwinConsolidatedBase(audioMaster, kNumPrograms, kNumParameters)
{
    p[kParamGain]      = 0.0f;    //  0 dB drive
    p[kParamCeiling]   = 1.0f;    //  0 dBFS
    p[kParamAttack]    = 0.527f;  //  ~60 ms (prototype voicing)
    p[kParamRelease]   = 0.5f;    //  1.0x
    p[kParamTransLink] = 0.7f;    //  70 %
    p[kParamTruePeak]  = 1.0f;    //  on
    p[kParamDelta]     = 0.0f;    //  off
    p[kParamOutput]    = 0.5f;    //  0 dB

    setNumInputs(kNumInputs);
    setNumOutputs(kNumOutputs);
    setUniqueID(kUniqueId);
    canProcessReplacing();
    canDoubleReplacing();
    reset();
}

bool FermentLimit::getEffectName(char* name)
{
    vst_strncpy(name, "FermentLimit", kVstMaxProductStrLen);
    return true;
}

void FermentLimit::setParameter(VstInt32 index, float value)
{
    if (index < 0 || index >= kNumParameters) return;
    if (!(value >= 0.0f)) value = 0.0f;   // also catches NaN
    if (value > 1.0f)     value = 1.0f;
    p[index] = value;
}

float FermentLimit::getParameter(VstInt32 index)
{
    if (index < 0 || index >= kNumParameters) return 0.0f;
    return p[index];
}

void FermentLimit::getParameterName(VstInt32 index, char* text)
{
    switch (index) {
        case kParamGain:      vst_strncpy(text, "Gain",    kVstMaxParamStrLen); break;
        case kParamCeiling:   vst_strncpy(text, "Ceiling", kVstMaxParamStrLen); break;
        case kParamAttack:    vst_strncpy(text, "Attack",  kVstMaxParamStrLen); break;
        case kParamRelease:   vst_strncpy(text, "Release", kVstMaxParamStrLen); break;
        case kParamTransLink: vst_strncpy(text, "TrLink",  kVstMaxParamStrLen); break;
        case kParamTruePeak:  vst_strncpy(text, "TruePk",  kVstMaxParamStrLen); break;
        case kParamDelta:     vst_strncpy(text, "Delta",   kVstMaxParamStrLen); break;
        case kParamOutput:    vst_strncpy(text, "Output",  kVstMaxParamStrLen); break;
        default: break;
    }
}

void FermentLimit::getParameterDisplay(VstInt32 index, char* text)
{
    const double v = (double)p[index];
    switch (index) {
        case kParamGain:
            snprintf(text, kVstMaxParamStrLen, "+%.1f", gainDbFromNorm(v));
            break;
        case kParamCeiling:
            snprintf(text, kVstMaxParamStrLen, "%.1f", ceilingDbFromNorm(v));
            break;
        case kParamAttack:
            snprintf(text, kVstMaxParamStrLen, "%.0f ms", attackMsFromNorm(v));
            break;
        case kParamRelease:
            snprintf(text, kVstMaxParamStrLen, "%.2fx", releaseScaleFromNorm(v));
            break;
        case kParamTransLink:
            snprintf(text, kVstMaxParamStrLen, "%.0f%%", v * 100.0);
            break;
        case kParamOutput:
            snprintf(text, kVstMaxParamStrLen, "%.1f", trimDbFromNorm(v));
            break;
        case kParamTruePeak:
        case kParamDelta:
            vst_strncpy(text, v >= 0.5 ? "On" : "Off", kVstMaxParamStrLen);
            break;
        default: break;
    }
}

void FermentLimit::getParameterLabel(VstInt32 index, char* text)
{
    switch (index) {
        case kParamGain:
        case kParamCeiling:
        case kParamOutput:
            vst_strncpy(text, "dB", kVstMaxParamStrLen);
            break;
        default:
            vst_strncpy(text, "", kVstMaxParamStrLen);
            break;
    }
}

bool FermentLimit::canConvertParameterTextToValue(VstInt32 index)
{
    switch (index) {
        case kParamTruePeak:
        case kParamDelta:
            return false;
        default:
            return true;
    }
}

bool FermentLimit::parameterTextToValue(VstInt32 index, const char* text, float& value)
{
    float parsed = 0.0f;
    if (!string2float(text, parsed)) return false;

    switch (index) {
        case kParamGain:
            value = (float)(parsed / 24.0);
            return true;
        case kParamCeiling:
            value = (float)((parsed + 24.0) / 24.0);
            return true;
        case kParamAttack:
            if (parsed < 10.0f) parsed = 10.0f;
            value = (float)(std::log(parsed / 10.0) / 3.4012);
            return true;
        case kParamRelease:
            if (parsed < 0.25f) parsed = 0.25f;
            value = (float)(std::log(parsed / 0.25) / 2.7726);
            return true;
        case kParamTransLink:
            value = (float)(parsed / 100.0);
            return true;
        case kParamOutput:
            value = (float)(parsed / 48.0 + 0.5);
            return true;
        default:
            return false;
    }
}

} // namespace airwinconsolidated::FermentLimit

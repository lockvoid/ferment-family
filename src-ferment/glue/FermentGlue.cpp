/* ========================================
 *  FermentGlue - FermentGlue.cpp
 *  Copyright (c) airwindows, Airwindows uses the MIT license
 *
 *  SSL-style feed-forward bus compressor. Not a sonic clone of any
 *  commercial SSL emulation — uses the public-domain topology described
 *  in Giannoulis/Massberg/Reiss (JAES 2012) with a two-time-constant
 *  program-dependent Auto release akin to the SSL G-series hardware.
 * ======================================== */

#ifndef __FermentGlue_H
#include "FermentGlue.h"
#endif
#include <cmath>
#include <cstdlib>
#include <algorithm>
namespace airwinconsolidated::FermentGlue {

AudioEffect* createEffectInstance(audioMasterCallback audioMaster) {return new FermentGlue(audioMaster);}

FermentGlue::FermentGlue(audioMasterCallback audioMaster) :
    AudioEffectX(audioMaster, kNumPrograms, kNumParameters)
{
    A = 0.5;   // Threshold: -20 dB
    B = 0.0;   // Ratio: 2
    C = 0.3;   // Attack: moderate
    D = 0.5;   // Release: mid
    E = 0.0;   // Makeup: 0 dB
    F = 1.0;   // Range: unlimited
    G = 1.0;   // Dry/Wet: full wet
    H = 0.0;   // SideChn: off
    I = 0.2;   // SC HPF: ~60 Hz default
    J = 0.0;   // Soft clip: off

    envRMS = 0.0;
    envGR = 0.0;
    hpfL_z1 = 0.0;
    hpfR_z1 = 0.0;

    fpdL = 1.0; while (fpdL < 16386) fpdL = rand()*UINT32_MAX;
    fpdR = 1.0; while (fpdR < 16386) fpdR = rand()*UINT32_MAX;

    _canDo.insert("plugAsChannelInsert");
    _canDo.insert("plugAsSend");
    _canDo.insert("x2in2out");
    setNumInputs(kNumInputs);
    setNumOutputs(kNumOutputs);
    setUniqueID(kUniqueId);
    canProcessReplacing();
    canDoubleReplacing();
    programsAreChunks(true);
    vst_strncpy (_programName, "Default", kVstMaxProgNameLen);
}

FermentGlue::~FermentGlue() {}
VstInt32 FermentGlue::getVendorVersion () {return 1000;}
void FermentGlue::setProgramName(char *name) {vst_strncpy (_programName, name, kVstMaxProgNameLen);}
void FermentGlue::getProgramName(char *name) {vst_strncpy (name, _programName, kVstMaxProgNameLen);}

static float pinParameter(float data)
{
    if (data < 0.0f) return 0.0f;
    if (data > 1.0f) return 1.0f;
    return data;
}

void FermentGlue::setParameter(VstInt32 index, float value) {
    switch (index) {
        case kParamA: A = value; break;
        case kParamB: B = value; break;
        case kParamC: C = value; break;
        case kParamD: D = value; break;
        case kParamE: E = value; break;
        case kParamF: F = value; break;
        case kParamG: G = value; break;
        case kParamH: H = value; break;
        case kParamI: I = value; break;
        case kParamJ: J = value; break;
        default: break;
    }
}

float FermentGlue::getParameter(VstInt32 index) {
    switch (index) {
        case kParamA: return A; break;
        case kParamB: return B; break;
        case kParamC: return C; break;
        case kParamD: return D; break;
        case kParamE: return E; break;
        case kParamF: return F; break;
        case kParamG: return G; break;
        case kParamH: return H; break;
        case kParamI: return I; break;
        case kParamJ: return J; break;
        default: break;
    }
    return 0.0;
}

void FermentGlue::getParameterName(VstInt32 index, char *text) {
    switch (index) {
        case kParamA: vst_strncpy (text, "Threshold", kVstMaxParamStrLen); break;
        case kParamB: vst_strncpy (text, "Ratio", kVstMaxParamStrLen); break;
        case kParamC: vst_strncpy (text, "Attack", kVstMaxParamStrLen); break;
        case kParamD: vst_strncpy (text, "Release", kVstMaxParamStrLen); break;
        case kParamE: vst_strncpy (text, "Makeup", kVstMaxParamStrLen); break;
        case kParamF: vst_strncpy (text, "Range", kVstMaxParamStrLen); break;
        case kParamG: vst_strncpy (text, "Dry/Wet", kVstMaxParamStrLen); break;
        case kParamH: vst_strncpy (text, "SideChn", kVstMaxParamStrLen); break;
        case kParamI: vst_strncpy (text, "SC HPF", kVstMaxParamStrLen); break;
        case kParamJ: vst_strncpy (text, "SoftClip", kVstMaxParamStrLen); break;
        default: break;
    }
}

void FermentGlue::getParameterDisplay(VstInt32 index, char *text) {
    switch (index) {
        case kParamA: float2string ((A - 1.0f) * 40.0f, text, kVstMaxParamStrLen); break;
        case kParamB: {
            double r = (B < 0.33f) ? 2.0 : (B < 0.67f) ? 4.0 : 10.0;
            float2string((float)r, text, kVstMaxParamStrLen);
            break;
        }
        case kParamC: float2string (0.03f * powf(1000.0f, C), text, kVstMaxParamStrLen); break;  // ms
        case kParamD: {
            if (D >= 0.9f) vst_strncpy(text, "Auto", kVstMaxParamStrLen);
            else float2string(0.1f * powf(12.0f, D / 0.9f), text, kVstMaxParamStrLen);  // seconds
            break;
        }
        case kParamE: float2string (E * 24.0f, text, kVstMaxParamStrLen); break;
        case kParamF: float2string (F * 60.0f, text, kVstMaxParamStrLen); break;
        case kParamG: float2string (G, text, kVstMaxParamStrLen); break;
        case kParamH: vst_strncpy (text, (H >= 0.5f) ? "On" : "Off", kVstMaxParamStrLen); break;
        case kParamI: float2string (20.0f * powf(25.0f, I), text, kVstMaxParamStrLen); break;  // 20 .. 500 Hz
        case kParamJ: vst_strncpy (text, (J >= 0.5f) ? "On" : "Off", kVstMaxParamStrLen); break;
        default: break;
    }
}

void FermentGlue::getParameterLabel(VstInt32 index, char *text) {
    switch (index) {
        case kParamA: vst_strncpy (text, "dB", kVstMaxParamStrLen); break;
        case kParamB: vst_strncpy (text, ":1", kVstMaxParamStrLen); break;
        case kParamC: vst_strncpy (text, "ms", kVstMaxParamStrLen); break;
        case kParamD: vst_strncpy (text, "s", kVstMaxParamStrLen); break;
        case kParamE: vst_strncpy (text, "dB", kVstMaxParamStrLen); break;
        case kParamF: vst_strncpy (text, "dB", kVstMaxParamStrLen); break;
        case kParamG: vst_strncpy (text, "", kVstMaxParamStrLen); break;
        case kParamH: vst_strncpy (text, "", kVstMaxParamStrLen); break;
        case kParamI: vst_strncpy (text, "Hz", kVstMaxParamStrLen); break;
        case kParamJ: vst_strncpy (text, "", kVstMaxParamStrLen); break;
        default: break;
    }
}

VstInt32 FermentGlue::canDo(char *text)
{ return (_canDo.find(text) == _canDo.end()) ? -1: 1; }

bool FermentGlue::getEffectName(char* name) {
    vst_strncpy(name, "FermentGlue", kVstMaxProductStrLen); return true;
}

VstPlugCategory FermentGlue::getPlugCategory() {return kPlugCategEffect;}

bool FermentGlue::getProductString(char* text) {
    vst_strncpy (text, "airwindows FermentGlue", kVstMaxProductStrLen); return true;
}

bool FermentGlue::getVendorString(char* text) {
    vst_strncpy (text, "airwindows", kVstMaxVendorStrLen); return true;
}

bool FermentGlue::parameterTextToValue(VstInt32 index, const char *text, float &value) {
    switch(index) {
    case kParamA: { auto b = string2float(text, value); if (b) { value = (value / 40.0f) + 1.0f; } return b; break; }
    case kParamB: { auto b = string2float(text, value); return b; break; }
    case kParamC: { auto b = string2float(text, value); if (b && value > 0) { value = std::log(value / 0.03f) / std::log(1000.0f); } return b; break; }
    case kParamD: { auto b = string2float(text, value); if (b && value > 0) { value = 0.9f * (std::log(value / 0.1f) / std::log(12.0f)); } return b; break; }
    case kParamE: { auto b = string2float(text, value); if (b) { value /= 24.0f; } return b; break; }
    case kParamF: { auto b = string2float(text, value); if (b) { value /= 60.0f; } return b; break; }
    case kParamG: { auto b = string2float(text, value); return b; break; }
    case kParamH: { auto b = string2float(text, value); return b; break; }
    case kParamI: { auto b = string2float(text, value); if (b && value > 0) { value = std::log(value / 20.0f) / std::log(25.0f); } return b; break; }
    case kParamJ: { auto b = string2float(text, value); return b; break; }
    }
    return false;
}

bool FermentGlue::canConvertParameterTextToValue(VstInt32 index) {
    switch(index) {
        case kParamA: return true;
        case kParamB: return true;
        case kParamC: return true;
        case kParamD: return true;
        case kParamE: return true;
        case kParamF: return true;
        case kParamG: return true;
        case kParamH: return true;
        case kParamI: return true;
        case kParamJ: return true;
    }
    return false;
}
} // end namespace

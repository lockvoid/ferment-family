/* ========================================
 *  FermentGlue - FermentGlue.h
 *  SSL-style feed-forward bus compressor for the Airwindows consolidated
 *  plugin. Inspired by the Glue/SSL G topology but using Airwindows
 *  conventions; not a sonic clone of Cytomic's implementation.
 * ======================================== */

#ifndef __FermentGlue_FermentGlue_H
#define __FermentGlue_FermentGlue_H

#ifndef __audioeffect__
#include "../../src/airwin_consolidated_base.h"
#endif

#include <set>
#include <string>
#include <math.h>

namespace airwinconsolidated::FermentGlue {
enum {
    kParamA = 0,  // Threshold
    kParamB = 1,  // Ratio  (2 / 4 / 10)
    kParamC = 2,  // Attack
    kParamD = 3,  // Release (1.2s ... 0.1s ... Auto)
    kParamE = 4,  // Makeup
    kParamF = 5,  // Range (max GR)
    kParamG = 6,  // Dry/Wet
    kParamH = 7,  // SideChn On/Off
    kParamI = 8,  // SC HPF frequency (20 .. 500 Hz log)
    kParamJ = 9,  // Soft clip output ceiling On/Off
  kNumParameters = 10
};

const int kNumPrograms = 0;
const int kNumInputs = 4; // [0,1]=main L/R, [2,3]=sidechain L/R
const int kNumOutputs = 2;
const unsigned long kUniqueId = 'FmGl';

class FermentGlue :
    public AudioEffectX
{
public:
    FermentGlue(audioMasterCallback audioMaster);
    ~FermentGlue();
    virtual bool getEffectName(char* name);
    virtual VstPlugCategory getPlugCategory();
    virtual bool getProductString(char* text);
    virtual bool getVendorString(char* text);
    virtual VstInt32 getVendorVersion();
    virtual void processReplacing (float** inputs, float** outputs, VstInt32 sampleFrames);
    virtual void processDoubleReplacing (double** inputs, double** outputs, VstInt32 sampleFrames);
    virtual void getProgramName(char *name);
    virtual void setProgramName(char *name);
    virtual float getParameter(VstInt32 index);
    virtual void setParameter(VstInt32 index, float value);
    virtual void getParameterLabel(VstInt32 index, char *text);
    virtual void getParameterName(VstInt32 index, char *text);
    virtual void getParameterDisplay(VstInt32 index, char *text);
    virtual bool parameterTextToValue(VstInt32 index, const char *text, float &value);
    virtual bool canConvertParameterTextToValue(VstInt32 index);
    virtual VstInt32 canDo(char *text);

    // Gain reduction in the most recent block (dB, >= 0); for the editor's
    // needle.  Read-only accessor, mirroring FermentLimit::meterGrDb().
    double meterGrDb() const noexcept { return envGR; }

private:
    char _programName[kVstMaxProgNameLen + 1];
    std::set<std::string> _canDo;

    // DSP state
    double envRMS;   // squared-signal RMS integrator
    double envGR;    // gain-reduction envelope in dB (>=0)
    // Detector HPF state (one-pole, ~60 Hz) per channel — reduces bass over-triggering
    double hpfL_z1, hpfR_z1;

    uint32_t fpdL, fpdR;

    float A; // Threshold
    float B; // Ratio
    float C; // Attack
    float D; // Release (with Auto at top)
    float E; // Makeup
    float F; // Range
    float G; // Dry/Wet
    float H; // SideChn on/off
    float I; // SC HPF freq (20 .. 500 Hz)
    float J; // Soft clip on/off
};

#endif
} // end namespace

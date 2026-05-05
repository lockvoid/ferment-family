// Utility regression test — same intent as eq_equivalence_test.cpp but for
// FermentUtility. Runs both the JUCE Processor and the pure-C++ FermentUtility
// DSP through the same input + params, asserts bit-equality.

#include "../src-ferment/utility/FermentUtilityProcessor.h"
#include "../src-ferment/utility/FermentUtility.h"
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {
    constexpr double kSR = 48000.0;
    constexpr int    kBlock = 512;
    constexpr int    kBlocks = 8;
    constexpr double kTolerance = 1e-9;

    void fillTestInput(juce::AudioBuffer<float>& buf, uint64_t seed) {
        const int N = buf.getNumSamples();
        float* L = buf.getWritePointer(0);
        float* R = buf.getWritePointer(1);
        uint64_t s = seed;
        for (int n = 0; n < N; ++n) {
            s = s * 6364136223846793005ULL + 1442695040888963407ULL;
            L[n] = ((int32_t)(s >> 33)) / float(1ll << 31);
            s = s * 6364136223846793005ULL + 1442695040888963407ULL;
            R[n] = ((int32_t)(s >> 33)) / float(1ll << 31);
        }
    }

    // Non-trivial config: gain +6dB, balL -0.3, width 0.7 (~280%), bass-mono on @ 200Hz
    void setupJUCEParams(FermentUtilityProcessor& p) {
        auto set = [&](const char* id, float v) {
            if (auto* atom = p.apvts.getRawParameterValue(id)) atom->store(v);
        };
        set("gain",         (float)((6.0 + 35.0) / 70.0));   // +6 dB
        set("balL",         (float)((-0.3 + 1.0) / 2.0));    // -0.3
        set("balR",         0.5f);                           //  0
        set("width",        0.7f);                           // ~280%
        set("bassmono",     1.0f);
        set("bassmonofreq", (float)(std::log(200.0 / 40.0) / std::log(25.0)));  // 200 Hz
        set("dc",           1.0f);
    }

    void setupDSPParams(airwinconsolidated::FermentUtility::FermentUtility& dsp) {
        using namespace airwinconsolidated::FermentUtility;
        dsp.setParameter(kParamGain,         (6.0f + 35.0f) / 70.0f);
        dsp.setParameter(kParamBalanceL,     (-0.3f + 1.0f) / 2.0f);
        dsp.setParameter(kParamBalanceR,     0.5f);
        dsp.setParameter(kParamWidth,        0.7f);
        dsp.setParameter(kParamBassMono,     1.0f);
        dsp.setParameter(kParamBassMonoFreq, float(std::log(200.0 / 40.0) / std::log(25.0)));
        dsp.setParameter(kParamDC,           1.0f);
    }

    double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
        double m = 0.0;
        for (size_t i = 0; i < a.size(); ++i) m = std::max(m, std::fabs(double(a[i]) - double(b[i])));
        return m;
    }
}

int main() {
    FermentUtilityProcessor jproc;
    jproc.setPlayConfigDetails(2, 2, kSR, kBlock);
    jproc.prepareToPlay(kSR, kBlock);
    setupJUCEParams(jproc);

    juce::AudioBuffer<float> jBuf(2, kBlock);
    std::vector<float> jOutL, jOutR; jOutL.reserve(kBlock * kBlocks); jOutR.reserve(kBlock * kBlocks);
    juce::MidiBuffer mb;

    using DSP = airwinconsolidated::FermentUtility::FermentUtility;
    DSP dsp(0);
    dsp.setSampleRate((float)kSR);
    setupDSPParams(dsp);

    std::vector<float> dInL(kBlock), dInR(kBlock), dOutL(kBlock), dOutR(kBlock);
    std::vector<float> dCatL, dCatR; dCatL.reserve(kBlock * kBlocks); dCatR.reserve(kBlock * kBlocks);

    for (int b = 0; b < kBlocks; ++b) {
        const uint64_t seed = 0xBABEDEEDull + (uint64_t)b;
        fillTestInput(jBuf, seed);
        for (int n = 0; n < kBlock; ++n) {
            dInL[n] = jBuf.getReadPointer(0)[n];
            dInR[n] = jBuf.getReadPointer(1)[n];
        }
        jproc.processBlock(jBuf, mb);
        for (int n = 0; n < kBlock; ++n) {
            jOutL.push_back(jBuf.getReadPointer(0)[n]);
            jOutR.push_back(jBuf.getReadPointer(1)[n]);
        }
        float* in[2]  = { dInL.data(),  dInR.data()  };
        float* out[2] = { dOutL.data(), dOutR.data() };
        dsp.processReplacing(in, out, kBlock);
        for (int n = 0; n < kBlock; ++n) {
            dCatL.push_back(dOutL[n]);
            dCatR.push_back(dOutR[n]);
        }
    }

    const double dL = maxAbsDiff(jOutL, dCatL);
    const double dR = maxAbsDiff(jOutR, dCatR);
    std::printf("Utility equivalence test\n");
    std::printf("  L max-abs-diff  %.3e (tol %.1e)\n", dL, kTolerance);
    std::printf("  R max-abs-diff  %.3e (tol %.1e)\n", dR, kTolerance);
    const bool ok = dL <= kTolerance && dR <= kTolerance;
    std::printf("  %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

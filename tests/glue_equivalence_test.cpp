// Glue regression test — pre-refactor the JUCE Processor wraps Airwindows
// GlueBlue; post-refactor it wraps the new Ferment-owned FermentGlue (a
// verbatim copy of GlueBlue). Output must remain bit-identical.

#include "../src-ferment/glue/FermentGlueProcessor.h"
#include "../src-ferment/glue/FermentGlue.h"
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {
    constexpr double kSR = 48000.0;
    constexpr int    kBlock = 512;
    constexpr int    kBlocks = 8;
    // Glue's DSP uses sub-LSB dither (Airwindows fpdL/R) seeded from rand().
    // Two freshly-constructed instances see different seeds, so output diverges
    // at ~2^-21 (-126 dB) even though every other DSP step is identical. This
    // is below the audible / 24-bit noise floor — gating at 1e-5 catches real
    // DSP regressions without false-failing on the dither.
    constexpr double kTolerance = 1e-5;

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

    // Non-trivial bus comp config: 2:1 ratio, threshold -8 dB, fast attack,
    // medium release, makeup +2 dB, dry/wet 100% (full).
    void setupJUCEParams(FermentGlueProcessor& p) {
        auto set = [&](const char* id, float v) {
            if (auto* atom = p.apvts.getRawParameterValue(id)) atom->store(v);
        };
        set("threshold", 0.8f);   // -8 dB-ish (threshold scaled (A-1)*40 in GlueBlue)
        set("ratio",     0.0f);   // 2:1
        set("attack",    0.2f);
        set("release",   0.5f);
        set("makeup",    0.6f);
        set("range",     1.0f);
        set("drywet",    1.0f);
        set("sidechain", 0.0f);
        set("schpf",     0.2f);
        set("softclip",  0.0f);
    }

    void setupDSPParams(airwinconsolidated::FermentGlue::FermentGlue& dsp) {
        // GlueBlue/FermentGlue: 10 params, indexed 0..9, identical order to the
        // FermentGlueProcessor APVTS layout.
        dsp.setParameter(0, 0.8f);   // threshold
        dsp.setParameter(1, 0.0f);   // ratio
        dsp.setParameter(2, 0.2f);   // attack
        dsp.setParameter(3, 0.5f);   // release
        dsp.setParameter(4, 0.6f);   // makeup
        dsp.setParameter(5, 1.0f);   // range
        dsp.setParameter(6, 1.0f);   // drywet
        dsp.setParameter(7, 0.0f);   // sidechain
        dsp.setParameter(8, 0.2f);   // schpf
        dsp.setParameter(9, 0.0f);   // softclip
    }

    double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
        double m = 0.0;
        for (size_t i = 0; i < a.size(); ++i) m = std::max(m, std::fabs(double(a[i]) - double(b[i])));
        return m;
    }
}

int main() {
    FermentGlueProcessor jproc;
    jproc.setPlayConfigDetails(2, 2, kSR, kBlock);
    jproc.prepareToPlay(kSR, kBlock);
    setupJUCEParams(jproc);

    juce::AudioBuffer<float> jBuf(2, kBlock);
    std::vector<float> jOutL, jOutR; jOutL.reserve(kBlock * kBlocks); jOutR.reserve(kBlock * kBlocks);
    juce::MidiBuffer mb;

    using DSP = airwinconsolidated::FermentGlue::FermentGlue;
    DSP dsp(0);
    dsp.setSampleRate((float)kSR);
    setupDSPParams(dsp);

    std::vector<float> dInL(kBlock), dInR(kBlock), dOutL(kBlock), dOutR(kBlock);
    // Glue DSP reads 4 input channels (mains + sidechain). Provide silent SC
    // to match the JUCE wrapper's silentBuffer fallback.
    std::vector<float> scL(kBlock, 0.0f), scR(kBlock, 0.0f);
    std::vector<float> dCatL, dCatR; dCatL.reserve(kBlock * kBlocks); dCatR.reserve(kBlock * kBlocks);

    for (int b = 0; b < kBlocks; ++b) {
        const uint64_t seed = 0xDEAFBEEFull + (uint64_t)b;
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
        float* in[4]  = { dInL.data(),  dInR.data(),  scL.data(),  scR.data()  };
        float* out[2] = { dOutL.data(), dOutR.data() };
        dsp.processReplacing(in, out, kBlock);
        for (int n = 0; n < kBlock; ++n) {
            dCatL.push_back(dOutL[n]);
            dCatR.push_back(dOutR[n]);
        }
    }

    const double dL = maxAbsDiff(jOutL, dCatL);
    const double dR = maxAbsDiff(jOutR, dCatR);
    std::printf("Glue equivalence test\n");
    std::printf("  L max-abs-diff  %.3e (tol %.1e)\n", dL, kTolerance);
    std::printf("  R max-abs-diff  %.3e (tol %.1e)\n", dR, kTolerance);
    const bool ok = dL <= kTolerance && dR <= kTolerance;
    std::printf("  %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

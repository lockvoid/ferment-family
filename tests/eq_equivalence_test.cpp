// EQ regression test — proves the pure-C++ Ferment EQ DSP produces the same
// output as the current JUCE FermentEqProcessor for the same input and params.
//
// Run twice:
//   1. BEFORE the dual-layer refactor — verifies the standalone DSP file
//      mirrors the inline JUCE processBlockT byte-for-byte.
//   2. AFTER the refactor — the JUCE Processor now delegates to the DSP, so
//      both paths are the same code; the test still passes.
//
// A regression in either direction (DSP drift, wrapper param-mapping bug)
// flips this test to red.

#include "../src-ferment/eq/FermentEqProcessor.h"
#include "../src-ferment/eq/FermentEq.h"
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {
    constexpr double kSR = 48000.0;
    constexpr int    kBlock = 512;
    constexpr int    kBlocks = 8;       // 8 × 512 = 4096 samples
    constexpr double kTolerance = 1e-9; // double-precision noise floor

    // Deterministic pseudo-random stereo input: linear-congruential, seeded.
    // Same seed → same input on every run.
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

    // Apply a non-trivial param set to the JUCE Processor (band 1 LowCut on,
    // band 3 Bell +6 dB @ 1 kHz, band 7 HighShelf -3 dB, output -3 dB, etc).
    // Using the APVTS atoms so the value path matches actual host automation.
    void setupJUCEParams(FermentEqProcessor& p) {
        auto set = [&](const char* id, float v) {
            if (auto* atom = p.apvts.getRawParameterValue(id)) atom->store(v);
        };
        // Band 1 LowCut @ ~50 Hz, on
        set("b1_type", (float)FermentEqProcessor::LowCut);
        set("b1_freq", (float)FermentEqProcessor::freqToNorm(50.0));
        set("b1_q",    0.4f);
        set("b1_on",   1.0f);
        // Band 3 Bell +6 dB @ ~1 kHz, on (default Bell)
        set("b3_type", (float)FermentEqProcessor::Bell);
        set("b3_freq", (float)FermentEqProcessor::freqToNorm(1000.0));
        set("b3_gain", (float)((6.0 + 15.0) / 30.0));   // +6 dB
        set("b3_q",    0.4f);
        set("b3_on",   1.0f);
        // Band 7 HighShelf -3 dB @ 8 kHz, on
        set("b7_type", (float)FermentEqProcessor::HighShelf);
        set("b7_freq", (float)FermentEqProcessor::freqToNorm(8000.0));
        set("b7_gain", (float)((-3.0 + 15.0) / 30.0));
        set("b7_q",    0.4f);
        set("b7_on",   1.0f);
        // Output -3 dB
        set("output", (float)((-3.0 + 12.0) / 24.0));
    }

    // Mirror the same params onto the pure-C++ DSP. APVTS choice indices are
    // stored as ints (0..N-1); the DSP expects 0..1 normalised — convert.
    void setupDSPParams(airwinconsolidated::FermentEq::FermentEq& dsp) {
        using ENS = airwinconsolidated::FermentEq::FermentEq;
        const auto typeNorm = [](int t) {
            return float(t) / float(airwinconsolidated::FermentEq::kNumTypes - 1);
        };
        const auto freqNorm = [](double hz) {
            return float(std::log(hz / 20.0) / std::log(1000.0));
        };
        constexpr int Stride = airwinconsolidated::FermentEq::kBandStride;
        const int OffT = airwinconsolidated::FermentEq::kBandTypeOffset;
        const int OffF = airwinconsolidated::FermentEq::kBandFreqOffset;
        const int OffG = airwinconsolidated::FermentEq::kBandGainOffset;
        const int OffQ = airwinconsolidated::FermentEq::kBandQOffset;
        const int OffO = airwinconsolidated::FermentEq::kBandOnOffset;

        // Band 1 LowCut @ 50 Hz, on
        dsp.setParameter(0 * Stride + OffT, typeNorm(airwinconsolidated::FermentEq::LowCut));
        dsp.setParameter(0 * Stride + OffF, freqNorm(50.0));
        dsp.setParameter(0 * Stride + OffQ, 0.4f);
        dsp.setParameter(0 * Stride + OffO, 1.0f);
        // Band 3 Bell +6 dB @ 1 kHz, on
        dsp.setParameter(2 * Stride + OffT, typeNorm(airwinconsolidated::FermentEq::Bell));
        dsp.setParameter(2 * Stride + OffF, freqNorm(1000.0));
        dsp.setParameter(2 * Stride + OffG, (6.0f + 15.0f) / 30.0f);
        dsp.setParameter(2 * Stride + OffQ, 0.4f);
        dsp.setParameter(2 * Stride + OffO, 1.0f);
        // Band 7 HighShelf -3 dB @ 8 kHz, on
        dsp.setParameter(6 * Stride + OffT, typeNorm(airwinconsolidated::FermentEq::HighShelf));
        dsp.setParameter(6 * Stride + OffF, freqNorm(8000.0));
        dsp.setParameter(6 * Stride + OffG, (-3.0f + 15.0f) / 30.0f);
        dsp.setParameter(6 * Stride + OffQ, 0.4f);
        dsp.setParameter(6 * Stride + OffO, 1.0f);
        // Output -3 dB
        dsp.setParameter(airwinconsolidated::FermentEq::kParamOutput, (-3.0f + 12.0f) / 24.0f);
        (void)ENS::freqFromNorm;  // suppress unused-using warning
    }

    double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
        double m = 0.0;
        for (size_t i = 0; i < a.size(); ++i) m = std::max(m, std::fabs(double(a[i]) - double(b[i])));
        return m;
    }
}

int main() {
    // --- JUCE path
    FermentEqProcessor jproc;
    jproc.setPlayConfigDetails(2, 2, kSR, kBlock);
    jproc.prepareToPlay(kSR, kBlock);
    setupJUCEParams(jproc);

    juce::AudioBuffer<float> jBuf(2, kBlock);
    std::vector<float> jOutL, jOutR; jOutL.reserve(kBlock * kBlocks); jOutR.reserve(kBlock * kBlocks);
    juce::MidiBuffer mb;

    // --- DSP path
    using DSP = airwinconsolidated::FermentEq::FermentEq;
    DSP dsp(0);
    dsp.setSampleRate((float)kSR);
    setupDSPParams(dsp);

    std::vector<float> dInL(kBlock), dInR(kBlock), dOutL(kBlock), dOutR(kBlock);
    std::vector<float> dCatL, dCatR; dCatL.reserve(kBlock * kBlocks); dCatR.reserve(kBlock * kBlocks);

    for (int b = 0; b < kBlocks; ++b) {
        const uint64_t seed = 0xC0FFEE00ull + (uint64_t)b;
        // Same input pumped through both paths
        fillTestInput(jBuf, seed);
        for (int n = 0; n < kBlock; ++n) {
            dInL[n] = jBuf.getReadPointer(0)[n];
            dInR[n] = jBuf.getReadPointer(1)[n];
        }
        // JUCE
        jproc.processBlock(jBuf, mb);
        for (int n = 0; n < kBlock; ++n) {
            jOutL.push_back(jBuf.getReadPointer(0)[n]);
            jOutR.push_back(jBuf.getReadPointer(1)[n]);
        }
        // DSP
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
    std::printf("EQ equivalence test\n");
    std::printf("  L max-abs-diff  %.3e (tol %.1e)\n", dL, kTolerance);
    std::printf("  R max-abs-diff  %.3e (tol %.1e)\n", dR, kTolerance);

    const bool ok = dL <= kTolerance && dR <= kTolerance;
    std::printf("  %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

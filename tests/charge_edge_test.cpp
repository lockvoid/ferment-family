// Ferment Charge edge-case tests.
//
// charge_test.cpp checks that the DSP is *correct*. This file checks that it
// does not fall over when the host does something unusual: zero-length
// blocks, absurd sample rates, out-of-range or NaN parameters, subnormal and
// non-finite input, sample-rate changes mid-stream, and very large buffers.
//
// Every check runs the real DSP. Nothing here asserts that something "is
// handled" without exercising it.

#include "../src-ferment/charge/FermentCharge.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace
{
    using airwinconsolidated::FermentCharge::FermentCharge;
    namespace FCns = airwinconsolidated::FermentCharge;

    int fails = 0;

    void check(const char* name, bool ok, const char* detail = "")
    {
        if (!ok) ++fails;
        std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", name,
                    detail[0] ? " — " : "", detail);
    }

    struct Out { std::vector<double> l, r; };

    Out render(FermentCharge& dsp, const std::vector<double>& in, int frames)
    {
        std::vector<double> a = in, b = in;
        std::vector<double> sc1(in.size(), 0.0), sc2(in.size(), 0.0);
        std::vector<double> ol(in.size(), 0.0), orr(in.size(), 0.0);
        double* ins[4]  = { a.data(), b.data(), sc1.data(), sc2.data() };
        double* outs[2] = { ol.data(), orr.data() };
        dsp.processDoubleReplacing(ins, outs, frames);
        return { ol, orr };
    }

    bool allFinite(const std::vector<double>& v, double bound = 1e6)
    {
        for (double x : v)
            if (!std::isfinite(x) || std::fabs(x) > bound) return false;
        return true;
    }

    FermentCharge makeDsp(double sr = 48000.0)
    {
        FermentCharge dsp(0);
        dsp.setSampleRate((float)sr);
        return dsp;
    }

    void setAllExtreme(FermentCharge& dsp)
    {
        for (int i = 0; i < FCns::kNumParameters; ++i)
            dsp.setParameter(i, 1.0f);
    }

    std::vector<double> tone(double freq, double amp, int n, double sr)
    {
        std::vector<double> v(n);
        for (int i = 0; i < n; ++i)
            v[i] = amp * std::sin(2.0 * M_PI * freq * (double)i / sr);
        return v;
    }

    // ---------------------------------------------------------------------

    void testZeroLengthBlock()
    {
        std::printf("\nZero-length and single-sample blocks\n");
        auto dsp = makeDsp();
        setAllExtreme(dsp);
        std::vector<double> in(16, 0.25);

        auto out = render(dsp, in, 0);
        bool untouched = true;
        for (double v : out.l) if (v != 0.0) untouched = false;
        check("zero frames writes nothing and does not crash", untouched);

        auto one = render(dsp, in, 1);
        check("single-frame block produces a finite sample", std::isfinite(one.l[0]));
    }

    void testSampleRates()
    {
        std::printf("\nSample rates from 8 kHz to 384 kHz\n");
        for (double sr : { 8000.0, 22050.0, 44100.0, 48000.0, 96000.0,
                           192000.0, 384000.0 }) {
            auto dsp = makeDsp(sr);
            setAllExtreme(dsp);
            dsp.setParameter(FCns::kParamMix, 1.0f);
            const int n = (int)(0.1 * sr);
            auto in = tone(220.0, 0.3, n, sr);
            auto out = render(dsp, in, n);
            char buf[96];
            std::snprintf(buf, sizeof buf, "%.0f Hz", sr);
            check("output stays finite and bounded", allFinite(out.l, 100.0), buf);
        }
    }

    void testCharacterModesAtLowRates()
    {
        std::printf("\nEvery Character mode at low sample rates\n");
        // Fat's high shelf is at 8423 Hz, above Nyquist at 8 kHz. Setting all
        // parameters to 1.0 selects Bright (2444 Hz) and misses this entirely,
        // so walk the modes explicitly.
        for (double sr : { 8000.0, 11025.0, 16000.0, 22050.0 }) {
            for (float mode : { 0.0f, 0.5f, 1.0f }) {
                auto dsp = makeDsp(sr);
                dsp.setParameter(FCns::kParamCharacter, 1.0f);
                dsp.setParameter(FCns::kParamCharMode, mode);
                dsp.setParameter(FCns::kParamMix, 1.0f);
                const int n = (int)(0.05 * sr);
                auto in = tone(220.0, 0.3, n, sr);
                auto out = render(dsp, in, n);
                char buf[96];
                std::snprintf(buf, sizeof buf, "%.0f Hz, char mode %.1f", sr, mode);
                check("stays finite and bounded", allFinite(out.l, 100.0), buf);
            }
        }
    }

    void testDegenerateSampleRate()
    {
        std::printf("\nDegenerate sample rate\n");
        // A host should never do this, but the DSP divides by sr when building
        // filter coefficients and computing time constants, so prove it does
        // not emit NaN into the audio stream if it happens.
        for (double sr : { 0.0, -48000.0, 1.0 }) {
            auto dsp = makeDsp(sr);
            setAllExtreme(dsp);
            auto in = tone(220.0, 0.3, 256, 48000.0);
            auto out = render(dsp, in, 256);
            char buf[96];
            std::snprintf(buf, sizeof buf, "sr=%.0f", sr);
            check("no NaN or Inf reaches the output", allFinite(out.l, 1e9), buf);
        }
    }

    void testParameterRange()
    {
        std::printf("\nOut-of-range, NaN and out-of-bounds parameters\n");
        auto in = tone(220.0, 0.3, 4096, 48000.0);

        for (float v : { -1.0f, 2.0f, 1e9f, -1e9f }) {
            auto dsp = makeDsp();
            for (int i = 0; i < FCns::kNumParameters; ++i)
                dsp.setParameter(i, v);
            auto out = render(dsp, in, 4096);
            char buf[96];
            std::snprintf(buf, sizeof buf, "all params = %g", v);
            check("output stays finite", allFinite(out.l, 1e9), buf);
        }

        {
            auto dsp = makeDsp();
            const float nan = std::numeric_limits<float>::quiet_NaN();
            for (int i = 0; i < FCns::kNumParameters; ++i)
                dsp.setParameter(i, nan);
            auto out = render(dsp, in, 4096);
            // NaN parameters producing NaN audio is defensible, but silence or
            // a finite signal is much better host behaviour. Record which.
            std::printf("       NaN params -> output %s\n",
                        allFinite(out.l, 1e9) ? "finite" : "NON-FINITE");
        }

        {
            // Out-of-bounds indices must be ignored, not corrupt memory.
            auto dsp = makeDsp();
            dsp.setParameter(-1, 0.5f);
            dsp.setParameter(FCns::kNumParameters, 0.5f);
            dsp.setParameter(9999, 0.5f);
            check("out-of-bounds parameter index is ignored",
                  dsp.getParameter(-1) == 0.0f
                  && dsp.getParameter(9999) == 0.0f);
        }
    }

    void testPathologicalInput()
    {
        std::printf("\nSubnormal, silent, DC and non-finite input\n");
        auto dsp0 = makeDsp();
        setAllExtreme(dsp0);
        std::vector<double> sub(4096, 1e-310);          // subnormal doubles
        auto out = render(dsp0, sub, 4096);
        check("subnormal input stays finite", allFinite(out.l, 1.0));

        auto dsp1 = makeDsp();
        setAllExtreme(dsp1);
        std::vector<double> dc(48000, 0.5);
        auto out1 = render(dsp1, dc, 48000);
        check("sustained DC stays bounded", allFinite(out1.l, 100.0));

        auto dsp2 = makeDsp();
        setAllExtreme(dsp2);
        std::vector<double> spike(4096, 0.0);
        spike[100] = 1e6;                                // absurd transient
        auto out2 = render(dsp2, spike, 4096);
        check("huge transient does not destabilise the envelope",
              allFinite(out2.l, 1e9));

        {
            // Non-finite input: document behaviour rather than assert a policy.
            auto dsp = makeDsp();
            setAllExtreme(dsp);
            std::vector<double> bad(1024, 0.1);
            bad[10] = std::numeric_limits<double>::quiet_NaN();
            bad[20] = std::numeric_limits<double>::infinity();
            auto o = render(dsp, bad, 1024);
            int nonFinite = 0;
            for (double v : o.l) if (!std::isfinite(v)) ++nonFinite;
            std::printf("       NaN/Inf input -> %d of 1024 output samples "
                        "non-finite\n", nonFinite);
        }
    }

    void testSampleRateChangeMidStream()
    {
        std::printf("\nSample-rate change between blocks\n");
        auto dsp = makeDsp(48000.0);
        setAllExtreme(dsp);
        dsp.setParameter(FCns::kParamMix, 1.0f);

        auto in = tone(220.0, 0.3, 4096, 48000.0);
        render(dsp, in, 4096);
        dsp.setSampleRate(96000.0);
        auto out = render(dsp, in, 4096);
        check("filters rebuild cleanly at the new rate", allFinite(out.l, 100.0));

        dsp.setSampleRate(48000.0);
        auto back = render(dsp, in, 4096);
        check("switching back stays clean", allFinite(back.l, 100.0));
    }

    void testLargeBlock()
    {
        std::printf("\nVery large block\n");
        auto dsp = makeDsp();
        setAllExtreme(dsp);
        dsp.setParameter(FCns::kParamMix, 1.0f);
        const int n = 1 << 20;                            // ~21.8 s at 48 kHz
        auto in = tone(220.0, 0.3, n, 48000.0);
        auto out = render(dsp, in, n);
        check("1,048,576-sample block stays finite", allFinite(out.l, 100.0));
    }

    void testBlockSizeInvariance()
    {
        std::printf("\nOutput does not depend on how the buffer is split\n");
        // Coefficients are derived once per process call, so a host using
        // small blocks must not get different audio from one using large ones.
        const int n = 24000;
        auto in = tone(220.0, 0.3, n, 48000.0);

        for (float comp : { 0.0f, 1.0f }) {
        auto whole = makeDsp();
        whole.setParameter(FCns::kParamCompression, comp);
        whole.setParameter(FCns::kParamMix, 1.0f);
        auto ref = render(whole, in, n);

        for (int block : { 1, 17, 64, 512 }) {
            auto dsp = makeDsp();
            dsp.setParameter(FCns::kParamCompression, comp);
            dsp.setParameter(FCns::kParamMix, 1.0f);

            std::vector<double> got;
            got.reserve(n);
            for (int i = 0; i < n; i += block) {
                const int len = std::min(block, n - i);
                std::vector<double> chunk(in.begin() + i, in.begin() + i + len);
                auto o = render(dsp, chunk, len);
                got.insert(got.end(), o.l.begin(), o.l.begin() + len);
            }

            double worst = 0.0;
            for (int i = 0; i < n; ++i)
                worst = std::max(worst, std::fabs(got[i] - ref.l[i]));
            char buf[96];
            double peak = 0.0;
            for (int i = 0; i < n; ++i) peak = std::max(peak, std::fabs(ref.l[i]));
            const double belowPeakDb = 20.0 * std::log10(worst / peak + 1e-300);
            // Not bit-exactness: the compiler vectorises a long loop and not a
            // one-sample one, and floating-point addition is not associative,
            // so the recursive envelope and filters diverge in the last bits.
            // That scales with recursive gain (-150 dB at Compression 1.00,
            // -128 dB at 10.00) and is inaudible. A genuine block-size bug
            // would show up tens of dB higher.
            std::snprintf(buf, sizeof buf, "comp=%.1f block=%d: %.1f dB below peak",
                          comp, block, belowPeakDb);
            check("split render matches single-call to better than -100 dB",
                  belowPeakDb < -100.0, buf);
        }
        }
    }
}

int main()
{
    std::printf("Ferment Charge edge-case tests\n");
    testZeroLengthBlock();
    testSampleRates();
    testCharacterModesAtLowRates();
    testDegenerateSampleRate();
    testParameterRange();
    testPathologicalInput();
    testSampleRateChangeMidStream();
    testLargeBlock();
    testBlockSizeInvariance();
    std::printf("\n%s\n", fails == 0 ? "ALL EDGE TESTS PASSED" : "EDGE TESTS FAILED");
    return fails == 0 ? 0 : 1;
}

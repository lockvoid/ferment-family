// Ferment Limit DSP tests.
//
// Mirrors the numpy reference suite (the validated numpy reference) plus
// C++ plumbing checks. The no-overshoot guarantee is tested *against the
// math bound*, not the hygiene clamp — samples between the TP-margin bound
// and the ceiling mean the min/smoother alignment silently broke (that
// failure mode cost 1.9 dB of true peak in the prototype).

#include "../src-ferment/limit/FermentLimit.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

namespace
{
    using airwinconsolidated::FermentLimit::FermentLimit;
    namespace FL = airwinconsolidated::FermentLimit;

    constexpr double kSR = 48000.0;
    int fails = 0;

    void check(const char* name, bool ok, const char* detail = "")
    {
        if (!ok) ++fails;
        std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", name,
                    detail[0] ? " — " : "", detail);
    }

    FermentLimit makeDsp()
    {
        FermentLimit dsp(0);
        dsp.setSampleRate((float)kSR);
        dsp.reset();
        return dsp;
    }

    struct Rendered { std::vector<double> l, r; };

    Rendered render(FermentLimit& dsp, const std::vector<double>& inL,
                    const std::vector<double>& inR)
    {
        const int n = (int)inL.size();
        std::vector<double> outL(n), outR(n);
        std::vector<double> copyL = inL, copyR = inR;
        double* ins[2]  = { copyL.data(), copyR.data() };
        double* outs[2] = { outL.data(), outR.data() };
        dsp.processDoubleReplacing(ins, outs, n);
        return { outL, outR };
    }

    std::vector<double> sine(double freq, double db, double seconds)
    {
        const int n = (int)(seconds * kSR);
        std::vector<double> v(n);
        const double amp = std::pow(10.0, db / 20.0);
        for (int i = 0; i < n; ++i)
            v[i] = amp * std::sin(2.0 * M_PI * freq * (double)i / kSR);
        return v;
    }

    // Pink-ish clipped noise — the music-shaped torture signal.
    std::vector<double> clippedPink(int n, unsigned seed)
    {
        std::vector<double> v(n);
        unsigned s = seed;
        double y = 0.0, mx = 0.0;
        for (int i = 0; i < n; ++i) {
            s = s * 1664525u + 1013904223u;
            const double w = 2.0 * ((double)s / 4294967296.0) - 1.0;
            y = w + 0.95 * y;
            v[i] = y;
            mx = std::max(mx, std::fabs(y));
        }
        for (double& x : v) x = std::max(-1.0, std::min(1.0, x * 2.0 / mx));
        return v;
    }

    double peak(const std::vector<double>& v)
    {
        double m = 0.0;
        for (double x : v) m = std::max(m, std::fabs(x));
        return m;
    }

    double rmsDb(const std::vector<double>& v, double from = 0.25, double to = 1.0)
    {
        const int a = (int)(from * (double)v.size());
        const int b = (int)(to * (double)v.size());
        double acc = 0.0;
        for (int i = a; i < b; ++i) acc += v[i] * v[i];
        return 10.0 * std::log10(acc / (double)(b - a) + 1e-30);
    }

    double toneDb(const std::vector<double>& v, double freq)
    {
        const int a = (int)(0.25 * (double)v.size());
        const int n = (int)v.size() - a;
        std::complex<double> acc(0.0, 0.0);
        for (int i = 0; i < n; ++i) {
            const double ph = 2.0 * M_PI * freq * (double)i / kSR;
            acc += v[a + i] * std::complex<double>(std::cos(ph), -std::sin(ph));
        }
        return 20.0 * std::log10(2.0 * std::abs(acc) / (double)n + 1e-30);
    }

    // 4x true-peak measurement (32 taps/phase — sharper than the DSP's own
    // 24-tap estimator, so it is a fair external check).
    double truePeakDb(const std::vector<double>& v)
    {
        const int tpp = 32, n = tpp * 4;
        const double center = (n - 1) / 2.0;
        double mx = 0.0;
        std::vector<double> phase[4];
        for (int ph = 0; ph < 4; ++ph) {
            phase[ph].resize(tpp);
            double s = 0.0;
            for (int t = 0; t < tpp; ++t) {
                const int i = ph + 4 * t;
                const double x = (i - center) / 4.0;
                const double sinc = std::fabs(x) < 1e-12 ? 1.0
                                    : std::sin(M_PI * x) / (M_PI * x);
                const double hann = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1));
                phase[ph][t] = sinc * hann;
                s += phase[ph][t];
            }
            for (int t = 0; t < tpp; ++t) phase[ph][t] /= s;
        }
        for (size_t m = tpp; m < v.size(); ++m) {
            for (int ph = 0; ph < 4; ++ph) {
                double acc = 0.0;
                for (int t = 0; t < tpp; ++t) acc += phase[ph][t] * v[m - t];
                mx = std::max(mx, std::fabs(acc));
            }
        }
        return 20.0 * std::log10(mx + 1e-30);
    }

    // ---- tests -----------------------------------------------------------

    void testTransparent()
    {
        std::printf("transparency below ceiling:\n");
        auto dsp = makeDsp();
        dsp.setParameter(FL::kParamCeiling, (float)((-1.0 + 24.0) / 24.0));
        auto in = sine(997.0, -6.0, 1.0);
        auto out = render(dsp, in, in);
        const int d = FermentLimit::latencySamples(kSR);
        double err = 0.0;
        for (size_t i = (size_t)d; i < in.size(); ++i)
            err = std::max(err, std::fabs(out.l[i] - in[i - d]));
        char buf[80];
        std::snprintf(buf, sizeof buf, "max err %.2e", err);
        check("output == delayed input", err < 1e-12, buf);
    }

    void testSamplePeakBound()
    {
        std::printf("sample peak bound (TP off):\n");
        auto dsp = makeDsp();
        dsp.setParameter(FL::kParamCeiling, (float)((-1.0 + 24.0) / 24.0));
        dsp.setParameter(FL::kParamTruePeak, 0.0f);
        auto in = clippedPink(4 * 48000, 3);
        auto out = render(dsp, in, in);
        check("peak <= ceiling", peak(out.l) <= std::pow(10.0, -1.0 / 20.0) + 1e-9);
    }

    void testGuaranteeNotMaskedByClamp()
    {
        std::printf("math bound vs hygiene clamp (TP on):\n");
        auto dsp = makeDsp();
        dsp.setParameter(FL::kParamCeiling, (float)((-1.0 + 24.0) / 24.0));
        auto in = clippedPink(2 * 48000, 7);
        auto out = render(dsp, in, in);
        const double bound = std::pow(10.0, (-1.0 - FL::kTpMarginDb) / 20.0);
        int over = 0;
        for (double x : out.l) if (std::fabs(x) > bound + 1e-9) ++over;
        char buf[80];
        std::snprintf(buf, sizeof buf, "%d samples above bound", over);
        check("no samples above the math bound", over == 0, buf);
    }

    void testTruePeakMusic()
    {
        std::printf("true peak on music-shaped material:\n");
        auto dsp = makeDsp();
        dsp.setParameter(FL::kParamCeiling, (float)((-1.0 + 24.0) / 24.0));
        auto in = clippedPink(3 * 48000, 1);
        auto out = render(dsp, in, in);
        const double tp = truePeakDb(out.l);
        char buf[80];
        std::snprintf(buf, sizeof buf, "%.2f dBTP (ceiling -1.0)", tp);
        check("TP <= ceiling + 0.05", tp <= -1.0 + 0.05, buf);
    }

    void testDualStageRelease()
    {
        std::printf("program-dependent release:\n");
        auto quiet = sine(220.0, -12.0, 4.0);
        auto recovery = [&](double burstS) -> double {
            auto x = sine(220.0, 3.0, burstS);
            x.insert(x.end(), quiet.begin(), quiet.end());
            auto dsp = makeDsp();
            dsp.setParameter(FL::kParamCeiling, (float)((-1.0 + 24.0) / 24.0));
            dsp.setParameter(FL::kParamTruePeak, 0.0f);
            auto out = render(dsp, x, x);
            const int n0 = (int)(burstS * kSR) + 400;
            const double target = 0.9 * std::pow(10.0, -12.0 / 20.0);
            for (int i = n0; i + 480 < (int)out.l.size(); i += 480) {
                double m = 0.0;
                for (int j = i; j < i + 480; ++j)
                    m = std::max(m, std::fabs(out.l[j]));
                if (m > target) return (i - n0) / kSR * 1000.0;
            }
            return 1e9;
        };
        const double fast = recovery(0.01);
        const double slow = recovery(2.0);
        char buf[100];
        std::snprintf(buf, sizeof buf, "10 ms hit: %.0f ms, 2 s pad: %.0f ms",
                      fast, slow);
        check("transient recovers fast", fast < 200.0, buf);
        check("sustained recovers slower", slow > fast * 1.5, buf);
    }

    void testBassNoRipple()
    {
        std::printf("no gain ripple on sustained bass:\n");
        auto dsp = makeDsp();
        dsp.setParameter(FL::kParamCeiling, (float)((-1.0 + 24.0) / 24.0));
        dsp.setParameter(FL::kParamTruePeak, 0.0f);
        auto in = sine(60.0, 3.0, 2.0);
        auto out = render(dsp, in, in);
        const double f1 = toneDb(out.l, 60.0);
        const double h = std::max(toneDb(out.l, 120.0), toneDb(out.l, 180.0));
        char buf[80];
        std::snprintf(buf, sizeof buf, "harmonics at %.1f dBc", h - f1);
        check("harmonics < -40 dBc", h - f1 < -40.0, buf);
    }

    void testLatency()
    {
        std::printf("latency:\n");
        auto dsp = makeDsp();
        std::vector<double> imp(8192, 0.0);
        imp[1000] = 0.25;
        auto out = render(dsp, imp, imp);
        int argmax = 0;
        for (int i = 0; i < (int)out.l.size(); ++i)
            if (std::fabs(out.l[i]) > std::fabs(out.l[argmax])) argmax = i;
        char buf[80];
        std::snprintf(buf, sizeof buf, "peak at +%d, reported %d",
                      argmax - 1000, FermentLimit::latencySamples(kSR));
        check("impulse lands at reported latency",
              argmax - 1000 == FermentLimit::latencySamples(kSR), buf);
    }

    void testDelta()
    {
        std::printf("delta:\n");
        auto d0 = makeDsp();
        d0.setParameter(FL::kParamCeiling, (float)((-1.0 + 24.0) / 24.0));
        d0.setParameter(FL::kParamDelta, 1.0f);
        auto quiet = sine(997.0, -12.0, 1.0);
        auto oq = render(d0, quiet, quiet);
        check("silent below ceiling", rmsDb(oq.l) < -60.0);

        auto d1 = makeDsp();
        d1.setParameter(FL::kParamCeiling, (float)((-1.0 + 24.0) / 24.0));
        d1.setParameter(FL::kParamDelta, 1.0f);
        auto loud = sine(220.0, 3.0, 1.0);
        auto ol = render(d1, loud, loud);
        check("audible when limiting", rmsDb(ol.l) > -30.0);
    }

    void testHygiene()
    {
        std::printf("numerical hygiene:\n");
        auto dsp = makeDsp();
        dsp.setParameter(FL::kParamGain, 0.5f);   // +12 dB drive
        auto in = sine(997.0, 0.0, 1.0);
        auto out = render(dsp, in, in);
        bool finite = true;
        for (double x : out.l) if (!std::isfinite(x)) finite = false;
        check("finite at +12 dB drive", finite);

        auto d2 = makeDsp();
        std::vector<double> silence(8192, 0.0);
        auto o2 = render(d2, silence, silence);
        check("silence in, silence out", peak(o2.l) < 1e-20);
    }

} // namespace

int main()
{
    std::printf("Ferment Limit DSP tests\n\n");
    testTransparent();
    testSamplePeakBound();
    testGuaranteeNotMaskedByClamp();
    testTruePeakMusic();
    testDualStageRelease();
    testBassNoRipple();
    testLatency();
    testDelta();
    testHygiene();
    std::printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "OK", fails,
                fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}

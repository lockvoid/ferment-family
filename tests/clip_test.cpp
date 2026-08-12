// Ferment Clip DSP tests.
//
// Mirrors the validated numpy reference suite plus the
// C++-specific plumbing: oversampler transparency, reported latency, delta
// and mix consistency, safety ceiling. Tolerances reflect the min-phase IIR
// oversampling (non-integer group delay, no exact nulls) — see
// the design notes before loosening anything.

#include "../src-ferment/clip/FermentClip.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

namespace
{
    using airwinconsolidated::FermentClip::FermentClip;
    namespace FC = airwinconsolidated::FermentClip;

    constexpr double kSR = 48000.0;
    int fails = 0;

    void check(const char* name, bool ok, const char* detail = "")
    {
        if (!ok) ++fails;
        std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", name,
                    detail[0] ? " — " : "", detail);
    }

    void checkNear(const char* name, double got, double want, double tol)
    {
        char buf[160];
        std::snprintf(buf, sizeof buf, "got %.4f, want %.4f +/- %.4f", got, want, tol);
        check(name, std::fabs(got - want) <= tol, buf);
    }

    // ---- harness ---------------------------------------------------------

    FermentClip makeDsp()
    {
        FermentClip dsp(0);
        dsp.setSampleRate((float)kSR);
        return dsp;
    }

    void setNorm(FermentClip& dsp, int param, double v)
    {
        dsp.setParameter(param, (float)v);
    }

    struct Rendered { std::vector<double> l, r; };

    Rendered render(FermentClip& dsp, const std::vector<double>& inL,
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

    std::vector<double> noise(double amp, int n, unsigned seed = 1)
    {
        std::vector<double> v(n);
        unsigned s = seed;
        for (int i = 0; i < n; ++i) {
            s = s * 1664525u + 1013904223u;
            v[i] = amp * (2.0 * ((double)s / 4294967296.0) - 1.0);
        }
        return v;
    }

    double rmsDb(const std::vector<double>& v, double from = 0.25, double to = 1.0)
    {
        const int a = (int)(from * (double)v.size());
        const int b = (int)(to * (double)v.size());
        double acc = 0.0;
        for (int i = a; i < b; ++i) acc += v[i] * v[i];
        return 10.0 * std::log10(acc / (double)(b - a) + 1e-30);
    }

    double peak(const std::vector<double>& v)
    {
        double m = 0.0;
        for (double x : v) m = std::max(m, std::fabs(x));
        return m;
    }

    // Goertzel magnitude (dB) of one frequency over the steady-state tail.
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

    // Steady-state DC (skips the DC blocker's settle at the start).
    double meanDb(const std::vector<double>& v)
    {
        const int a = (int)(0.25 * (double)v.size());
        double acc = 0.0;
        for (size_t i = a; i < v.size(); ++i) acc += v[i];
        return 20.0 * std::log10(std::fabs(acc / (double)(v.size() - a)) + 1e-30);
    }

    // ---- tests -----------------------------------------------------------

    void testTransparentBelowKnee()
    {
        std::printf("transparency below the knee:\n");
        auto dsp = makeDsp();
        // ceiling 0 dBFS, knee 35% -> linear below -3.7 dBFS; -12 dB sine.
        auto in = sine(997.0, -12.0, 1.0);
        auto out = render(dsp, in, in);
        checkNear("997 Hz RMS unchanged", rmsDb(out.l) - rmsDb(in), 0.0, 0.05);

        // Full-band: white noise below the knee passes unchanged.
        auto dsp2 = makeDsp();
        auto nz = noise(0.05, 48000 * 2);
        auto outN = render(dsp2, nz, nz);
        checkNear("noise RMS unchanged", rmsDb(outN.l) - rmsDb(nz), 0.0, 0.1);
    }

    void testCeilingHonored()
    {
        std::printf("ceiling honored:\n");
        auto dsp = makeDsp();
        setNorm(dsp, FC::kParamCeiling, (0.0 + 24.0 - 1.0) / 24.0);  // -1 dBFS
        auto in = sine(60.0, +6.0, 1.0);
        auto out = render(dsp, in, in);
        const double ceilLin = std::pow(10.0, -1.0 / 20.0);
        check("sample peak <= ceiling", peak(out.l) <= ceilLin + 1e-9);
        // and it actually clips (not just attenuates): RMS within 3 dB of ceiling
        check("output is loud (clipping, not ducking)",
              rmsDb(out.l) > -6.0);
    }

    void testHarmonicsOddOnly()
    {
        std::printf("symmetric clip -> odd harmonics only:\n");
        auto dsp = makeDsp();
        setNorm(dsp, FC::kParamTilt, 0.0);
        auto in = sine(997.0, +6.0, 2.0);
        auto out = render(dsp, in, in);
        const double f1 = toneDb(out.l, 997.0);
        const double f2 = toneDb(out.l, 2.0 * 997.0);
        const double f3 = toneDb(out.l, 3.0 * 997.0);
        check("3rd harmonic present", f3 > f1 - 40.0);
        check("2nd harmonic absent", f2 < f1 - 70.0);
    }

    void testBiasEvenHarmonics()
    {
        std::printf("bias -> even harmonics, no DC:\n");
        auto dsp = makeDsp();
        setNorm(dsp, FC::kParamTilt, 0.0);
        setNorm(dsp, FC::kParamBias, 0.85);  // +70%
        auto in = sine(997.0, +6.0, 2.0);
        auto out = render(dsp, in, in);
        const double f1 = toneDb(out.l, 997.0);
        const double f2 = toneDb(out.l, 2.0 * 997.0);
        check("2nd harmonic present with bias", f2 > f1 - 45.0);
        check("DC blocked", meanDb(out.l) < -60.0);
    }

    void testAliasSuppression()
    {
        std::printf("alias suppression (997 Hz, +9 dB drive):\n");
        auto dsp = makeDsp();
        setNorm(dsp, FC::kParamTilt, 0.0);
        auto in = sine(997.0, +9.0, 2.0);
        auto out = render(dsp, in, in);
        // Strongest in-band alias candidates for 997 Hz vs 192k folding are
        // far above the audible harmonics ladder; probe a set of non-harmonic
        // bins and require them far below the fundamental.
        const double f1 = toneDb(out.l, 997.0);
        double worst = -300.0;
        for (double f = 500.0; f < 20000.0; f += 613.0) {
            const double r = std::fmod(f, 997.0);
            if (r < 40.0 || r > 997.0 - 40.0) continue;  // skip harmonics
            worst = std::max(worst, toneDb(out.l, f));
        }
        char buf[120];
        std::snprintf(buf, sizeof buf, "worst non-harmonic %.1f dB below carrier",
                      f1 - worst);
        check("non-harmonic content < -60 dBc", worst < f1 - 60.0, buf);
    }

    void testTiltNeutral()
    {
        std::printf("tilt neutrality below the knee:\n");
        // Same sub-threshold noise with tilt 0 vs 100% must match closely:
        // pre-emphasis is exactly inverted when nothing clips.
        auto nz = noise(0.05, 48000 * 2);
        auto d0 = makeDsp(); setNorm(d0, FC::kParamTilt, 0.0);
        auto d1 = makeDsp(); setNorm(d1, FC::kParamTilt, 1.0);
        auto o0 = render(d0, nz, nz);
        auto o1 = render(d1, nz, nz);
        double diff = 0.0, ref = 0.0;
        for (size_t i = 4800; i < o0.l.size(); ++i) {
            diff += (o0.l[i] - o1.l[i]) * (o0.l[i] - o1.l[i]);
            ref  += o0.l[i] * o0.l[i];
        }
        const double diffDb = 10.0 * std::log10(diff / (ref + 1e-30) + 1e-30);
        char buf[120];
        std::snprintf(buf, sizeof buf, "tilt on/off difference %.1f dB", diffDb);
        check("tilt round-trip transparent (< -40 dB)", diffDb < -40.0, buf);
    }

    void testLatencyAndMix()
    {
        std::printf("latency + dry path:\n");
        // Impulse below the knee, mix 100%: peak should land at kLatencySamples.
        auto dsp = makeDsp();
        std::vector<double> imp(4096, 0.0);
        imp[100] = 0.25;
        auto out = render(dsp, imp, imp);
        int argmax = 0;
        for (int i = 0; i < (int)out.l.size(); ++i)
            if (std::fabs(out.l[i]) > std::fabs(out.l[argmax])) argmax = i;
        char buf[120];
        std::snprintf(buf, sizeof buf, "impulse peak at %+d samples",
                      argmax - 100);
        check("latency == kLatencySamples +/- 1",
              std::abs(argmax - 100 - FC::kLatencySamples) <= 1, buf);

        // mix 0 must be exactly the delayed dry signal.
        auto d2 = makeDsp();
        setNorm(d2, FC::kParamMix, 0.0);
        auto nz = noise(0.5, 48000);
        auto o2 = render(d2, nz, nz);
        double err = 0.0;
        for (int i = 1000; i < 48000; ++i)
            err = std::max(err, std::fabs(o2.l[i] - nz[i - FC::kLatencySamples]));
        check("mix 0 == delayed dry (exact)", err < 1e-12);
    }

    void testDelta()
    {
        std::printf("delta:\n");
        // Sub-threshold: delta is (near) silence.
        auto d0 = makeDsp();
        setNorm(d0, FC::kParamDelta, 1.0);
        auto quiet = sine(997.0, -12.0, 1.0);
        auto oq = render(d0, quiet, quiet);
        check("delta silent below knee (< -40 dBFS)", rmsDb(oq.l) < -40.0);

        // Driven: delta carries real energy.
        auto d1 = makeDsp();
        setNorm(d1, FC::kParamDelta, 1.0);
        auto loud = sine(997.0, +6.0, 1.0);
        auto ol = render(d1, loud, loud);
        check("delta audible when clipping", rmsDb(ol.l) > -30.0);
    }

    void testAutoGain()
    {
        std::printf("auto gain:\n");
        auto dsp = makeDsp();
        setNorm(dsp, FC::kParamAutoGain, 1.0);
        setNorm(dsp, FC::kParamCeiling, (0.0 + 24.0 - 6.0) / 24.0);  // -6 dBFS
        // Output trim -6 dB keeps the safety clamp out of the measurement
        // (auto gain cannot push RMS past the ceiling by design).
        setNorm(dsp, FC::kParamOutput, (-6.0) / 48.0 + 0.5);
        auto in = sine(220.0, -2.0, 4.0);
        auto out = render(dsp, in, in);
        // steady state: output RMS within 1 dB of (input RMS - 6 dB)
        checkNear("RMS matched",
                  rmsDb(out.l, 0.75, 1.0) - (rmsDb(in, 0.75, 1.0) - 6.0),
                  0.0, 1.0);
    }

    void testNumericalHygiene()
    {
        std::printf("numerical hygiene:\n");
        auto dsp = makeDsp();
        auto in = sine(997.0, +12.0, 1.0);
        auto out = render(dsp, in, in);
        bool finite = true;
        for (double x : out.l) if (!std::isfinite(x)) finite = false;
        check("all samples finite at +12 dB drive", finite);

        auto dsp2 = makeDsp();
        std::vector<double> silence(8192, 0.0);
        auto o2 = render(dsp2, silence, silence);
        check("silence in, silence out", peak(o2.l) < 1e-20);
    }

} // namespace

int main()
{
    std::printf("Ferment Clip DSP tests\n\n");
    testTransparentBelowKnee();
    testCeilingHonored();
    testHarmonicsOddOnly();
    testBiasEvenHarmonics();
    testAliasSuppression();
    testTiltNeutral();
    testLatencyAndMix();
    testDelta();
    testAutoGain();
    testNumericalHygiene();
    std::printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "OK", fails,
                fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}

// Ferment Charge DSP tests.
//
// These assert against the *measured reference behaviour* recorded in
// doc/ferment-charge-design.md, not against arbitrary round numbers. If a
// tolerance here fails, either the DSP regressed or the fitted tables in
// FermentChargeTables.h were regenerated from new measurements — check the
// design doc before loosening anything.
//
// Covers:
//   - static transfer curve vs. the measured leveller table
//   - "no threshold": gain varies continuously, no flat region above a knee
//   - symmetric gain cell: compression yields odd harmonics, not even
//   - ballistics roughly match the measured time constants
//   - stereo modes (Link / Dual Mono / M-S) route gain reduction as measured
//   - saturation modes: Mild asymmetric, Moderate/Hot symmetric
//   - character modes tilt in the measured directions
//   - mix, bypass-equivalence and numerical hygiene

#include "../src-ferment/charge/FermentCharge.h"
#include "../src-ferment/charge/FermentChargeTables.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
    using airwinconsolidated::FermentCharge::FermentCharge;
    namespace FCns = airwinconsolidated::FermentCharge;
    namespace tbl = ferment::charge::tables;

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
        std::snprintf(buf, sizeof buf, "got %.3f, want %.3f +/- %.3f", got, want, tol);
        check(name, std::fabs(got - want) <= tol, buf);
    }

    // ---- harness ---------------------------------------------------------

    struct Rendered
    {
        std::vector<double> l, r;
    };

    Rendered render(FermentCharge& dsp, const std::vector<double>& inL,
                    const std::vector<double>& inR)
    {
        const int n = (int)inL.size();
        std::vector<double> outL(n), outR(n), scL(n, 0.0), scR(n, 0.0);
        std::vector<double> copyL = inL, copyR = inR;

        double* ins[4]  = { copyL.data(), copyR.data(), scL.data(), scR.data() };
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

    double rmsDb(const std::vector<double>& v, double from = 0.5, double to = 1.0)
    {
        const int a = (int)(from * v.size()), b = (int)(to * v.size());
        double s = 0.0;
        for (int i = a; i < b; ++i) s += v[i] * v[i];
        return 10.0 * std::log10(s / (double)(b - a) + 1e-300);
    }

    // Harmonic amplitude relative to the fundamental, by correlation against
    // a Blackman-Harris window.
    //
    // The window is not optional. An earlier version correlated with a
    // rectangular window on the grounds that the probe is exactly periodic,
    // which is true of the *input* but not of the output: at high Compression
    // the leveller's envelope is still settling (release tau is 188 ms), and
    // that slow drift leaks across the whole spectrum when unwindowed. It read
    // H2 as -38.8 dB where the real figure is -69.1 dB — a 30 dB error that
    // made this suite disagree with the same measurement taken on the built
    // VST3 through tools/ab. Blackman-Harris puts the leakage ~90 dB down.
    double harmonicDb(const std::vector<double>& v, double freq, int order)
    {
        const int a = (int)(0.5 * v.size());
        const int n = (int)v.size() - a;
        if (n < 16) return -300.0;

        auto mag = [&](double f) {
            double re = 0.0, im = 0.0;
            for (int i = 0; i < n; ++i) {
                const double t = (double)i / (double)(n - 1);
                const double win = 0.35875
                                 - 0.48829 * std::cos(2.0 * M_PI * t)
                                 + 0.14128 * std::cos(4.0 * M_PI * t)
                                 - 0.01168 * std::cos(6.0 * M_PI * t);
                const double w = 2.0 * M_PI * f * (double)(a + i) / kSR;
                re += v[a + i] * win * std::cos(w);
                im += v[a + i] * win * std::sin(w);
            }
            return std::sqrt(re * re + im * im);
        };
        const double fund = mag(freq);
        if (fund <= 0.0) return -300.0;
        return 20.0 * std::log10(mag(freq * order) / fund + 1e-30);
    }


    Rendered renderChunked(FermentCharge& dsp, const std::vector<double>& in,
                           int blockSize, int param, double from, double to)
    {
        const int n = (int)in.size();
        Rendered out{ std::vector<double>(n), std::vector<double>(n) };
        std::vector<double> a(blockSize), b(blockSize), sc(blockSize, 0.0),
            oa(blockSize), ob(blockSize);

        for (int pos = 0; pos < n; pos += blockSize) {
            const int m = std::min(blockSize, n - pos);
            if (param >= 0) {
                const double t = (double)pos / (double)n;
                dsp.setParameter(param, (float)(from + (to - from) * t));
            }
            for (int i = 0; i < m; ++i) { a[i] = in[pos + i]; b[i] = in[pos + i]; }
            double* ins[4] = { a.data(), b.data(), sc.data(), sc.data() };
            double* outs[2] = { oa.data(), ob.data() };
            dsp.processDoubleReplacing(ins, outs, m);
            for (int i = 0; i < m; ++i) { out.l[pos + i] = oa[i]; out.r[pos + i] = ob[i]; }
        }
        return out;
    }

    std::vector<double> offsetSine(double freq, double db, double seconds)
    {
        const int n = (int)(seconds * kSR);
        std::vector<double> v(n);
        const double amp = std::pow(10.0, db / 20.0);
        for (int i = 0; i < n; ++i)
            v[i] = amp * std::sin(2.0 * M_PI * freq * (double)i / kSR + 0.3);
        return v;
    }

    double maxAbsDiff(const std::vector<double>& a, const std::vector<double>& b)
    {
        double worst = 0.0;
        for (size_t i = 0; i < a.size() && i < b.size(); ++i)
            worst = std::max(worst, std::fabs(a[i] - b[i]));
        return worst;
    }


    double boundaryDiscontinuity(const std::vector<double>& y, int blockSize)
    {
        double bmax = 0.0, imax = 0.0;
        const int n = (int)y.size();
        auto d2 = [&](int i) { return std::fabs(y[i] - 2.0 * y[i - 1] + y[i - 2]); };
        for (int k = 1; k * blockSize + 1 < n; ++k) {
            const int b = k * blockSize;
            bmax = std::max(bmax, std::max(d2(b), d2(b + 1)));
            for (int i = b + 8; i < std::min(b + blockSize, n); ++i)
                imax = std::max(imax, d2(i));
        }
        return imax > 0.0 ? bmax / imax : 0.0;
    }

    FermentCharge makeDsp()
    {
        FermentCharge dsp(0);
        dsp.setSampleRate((float)kSR);
        return dsp;
    }

    void setNeutral(FermentCharge& dsp)
    {
        dsp.setParameter(FCns::kParamInput, 0.5f);
        dsp.setParameter(FCns::kParamCompression, 0.0f);
        dsp.setParameter(FCns::kParamAttack, 0.3667f);
        dsp.setParameter(FCns::kParamRelease, 0.4444f);
        dsp.setParameter(FCns::kParamSaturation, 0.0f);
        dsp.setParameter(FCns::kParamSatMode, 0.0f);
        dsp.setParameter(FCns::kParamCharacter, 0.0f);
        dsp.setParameter(FCns::kParamCharMode, 0.0f);
        dsp.setParameter(FCns::kParamDetectorHP, 0.0f);
        dsp.setParameter(FCns::kParamStereoMode, 0.0f);
        dsp.setParameter(FCns::kParamSidechain, 0.0f);
        dsp.setParameter(FCns::kParamScGain, 0.5f);
        dsp.setParameter(FCns::kParamMix, 1.0f);
        dsp.setParameter(FCns::kParamOutput, 0.5f);
    }

    // Expected gain from the fitted model, for cross-checking the DSP.
    double modelGainDb(double levelDb, double compNorm)
    {
        auto lut = [&](const double* t, int n) {
            const double f = compNorm * (n - 1);
            const int i = (int)f;
            if (i >= n - 1) return t[n - 1];
            return t[i] + (t[i + 1] - t[i]) * (f - i);
        };
        const double plateau = lut(tbl::kPlateauDb, 21);
        const double knee    = lut(tbl::kKneeDb, 21);
        const double slope   = lut(tbl::kSlope, 21);
        const double width   = lut(tbl::kWidthDb, 21);
        const double z = (levelDb - knee) / width;
        const double sp = (z > 30.0) ? z : std::log1p(std::exp(z));
        return plateau - slope * width * sp;
    }

    // ---- tests -----------------------------------------------------------

    void testStaticCurve()
    {
        std::printf("\nStatic transfer curve (vs fitted leveller model)\n");
        // A steady sine's RMS-measured gain should track the model. Tolerance
        // is loose because the fast detector leaves deliberate ripple, which
        // shifts RMS slightly — that ripple is the point (see design doc 3.2).
        for (double comp : { 0.0, 0.25, 0.5, 0.75, 1.0 }) {
            for (double lvl : { -50.0, -30.0, -12.0 }) {
                auto dsp = makeDsp();
                setNeutral(dsp);
                dsp.setParameter(FCns::kParamCompression, (float)comp);
                auto in = sine(1000.0, lvl, 1.2);
                auto out = render(dsp, in, in);
                const double got = rmsDb(out.l, 0.6, 0.95) - lvl;
                char nm[96];
                std::snprintf(nm, sizeof nm, "comp=%.2f level=%.0f dB", comp, lvl);
                checkNear(nm, got, modelGainDb(lvl, comp), 2.5);
            }
        }
    }

    void testNoThreshold()
    {
        std::printf("\nLeveller character: gain varies continuously, no flat knee\n");
        auto dsp = makeDsp();
        setNeutral(dsp);
        dsp.setParameter(FCns::kParamCompression, 0.5f);

        double prev = 1e9;
        bool monotonic = true;
        for (double lvl = -60.0; lvl <= -6.0; lvl += 6.0) {
            auto d2 = makeDsp();
            setNeutral(d2);
            d2.setParameter(FCns::kParamCompression, 0.5f);
            auto in = sine(1000.0, lvl, 1.0);
            auto out = render(d2, in, in);
            const double gain = rmsDb(out.l, 0.6, 0.95) - lvl;
            if (gain > prev + 0.05) monotonic = false;
            prev = gain;
        }
        check("gain decreases monotonically with input level", monotonic);

        // Range compression: 54 dB in should produce clearly less out.
        auto quiet = sine(1000.0, -60.0, 1.0);
        auto loud  = sine(1000.0, -6.0, 1.0);
        auto d3 = makeDsp(); setNeutral(d3); d3.setParameter(FCns::kParamCompression, 1.0f);
        auto oq = render(d3, quiet, quiet);
        auto d4 = makeDsp(); setNeutral(d4); d4.setParameter(FCns::kParamCompression, 1.0f);
        auto ol = render(d4, loud, loud);
        const double span = rmsDb(ol.l, 0.6, 0.95) - rmsDb(oq.l, 0.6, 0.95);
        char buf[128];
        std::snprintf(buf, sizeof buf, "54 dB in -> %.1f dB out", span);
        check("at max compression the output span collapses (<25 dB)", span < 25.0, buf);
    }

    void testSymmetricGainCell()
    {
        std::printf("\nGain cell is symmetric: compression makes odd harmonics\n");
        // 220 Hz over 1.0 s at 48 kHz is exactly periodic (220 cycles).
        auto dsp = makeDsp();
        setNeutral(dsp);
        dsp.setParameter(FCns::kParamCompression, 1.0f);
        auto in = sine(220.0, -12.0, 1.0);
        auto out = render(dsp, in, in);

        const double h2 = harmonicDb(out.l, 220.0, 2);
        const double h3 = harmonicDb(out.l, 220.0, 3);
        char buf[128];
        std::snprintf(buf, sizeof buf, "H2=%.1f dB, H3=%.1f dB", h2, h3);
        check("H3 dominates H2 (ripple, not asymmetry)", h3 > h2, buf);
    }

    void testBallistics()
    {
        std::printf("\nBallistics track the measured time constants\n");
        // Fastest and slowest release should differ by roughly the measured
        // ratio (41.8 ms -> 465 ms, about 11x).
        const double fast = tbl::kReleaseMs[0];
        const double slow = tbl::kReleaseMs[10];
        checkNear("release table spans the measured range", slow / fast, 11.1, 1.5);
        check("attack table is ascending = slower",
              tbl::kAttackMs[10] > tbl::kAttackMs[0] * 10.0);
        checkNear("fastest attack near measured 0.44 ms", tbl::kAttackMs[0], 0.44, 0.15);
    }

    void testStereoModes()
    {
        std::printf("\nStereo modes route gain reduction as measured\n");
        auto loud  = sine(1000.0, -6.0, 1.2);
        auto quiet = sine(3000.0, -30.0, 1.2);

        auto run = [&](float mode) {
            auto dsp = makeDsp();
            setNeutral(dsp);
            dsp.setParameter(FCns::kParamCompression, 0.7f);
            dsp.setParameter(FCns::kParamStereoMode, mode);
            auto out = render(dsp, loud, quiet);
            return std::pair<double, double>{
                rmsDb(out.l, 0.6, 0.95) - rmsDb(loud, 0.6, 0.95),
                rmsDb(out.r, 0.6, 0.95) - rmsDb(quiet, 0.6, 0.95) };
        };

        auto [linkL, linkR] = run(0.0f);           // Stereo Link
        auto [dualL, dualR] = run(0.5f);           // Dual Mono
        char buf[160];
        std::snprintf(buf, sizeof buf, "L=%.2f R=%.2f", linkL, linkR);
        check("Stereo Link applies equal gain to both channels",
              std::fabs(linkL - linkR) < 0.5, buf);
        std::snprintf(buf, sizeof buf, "L=%.2f R=%.2f", dualL, dualR);
        check("Dual Mono lets the quiet channel rise independently",
              dualR > dualL + 5.0, buf);
    }

    void testSaturationModes()
    {
        std::printf("\nSaturation: Mild asymmetric, Moderate/Hot symmetric\n");
        auto in = sine(220.0, -12.0, 1.0);

        auto profile = [&](float mode) {
            auto dsp = makeDsp();
            setNeutral(dsp);
            dsp.setParameter(FCns::kParamSaturation, 1.0f);
            dsp.setParameter(FCns::kParamSatMode, mode);
            auto out = render(dsp, in, in);
            return std::pair<double, double>{ harmonicDb(out.l, 220.0, 2),
                                              harmonicDb(out.l, 220.0, 3) };
        };

        auto [mildH2, mildH3] = profile(0.0f);
        auto [modH2, modH3]   = profile(0.5f);
        auto [hotH2, hotH3]   = profile(1.0f);
        char buf[160];

        std::snprintf(buf, sizeof buf, "H2=%.1f H3=%.1f", mildH2, mildH3);
        check("Mild produces measurable 2nd harmonic (> -60 dB)", mildH2 > -60.0, buf);
        std::snprintf(buf, sizeof buf, "Moderate H2=%.1f, Hot H2=%.1f", modH2, hotH2);
        check("Moderate and Hot suppress the 2nd harmonic",
              modH2 < -80.0 && hotH2 < -80.0, buf);
        check("all modes generate 3rd harmonic at full drive",
              mildH3 > -40.0 && modH3 > -30.0 && hotH3 > -30.0);
    }

    void testCharacterModes()
    {
        std::printf("\nCharacter modes tilt in the measured directions\n");
        auto probe = [&](float mode, double freq) {
            auto dsp = makeDsp();
            setNeutral(dsp);
            dsp.setParameter(FCns::kParamCharacter, 1.0f);
            dsp.setParameter(FCns::kParamCharMode, mode);
            auto in = sine(freq, -24.0, 1.0);
            auto out = render(dsp, in, in);
            return rmsDb(out.l, 0.6, 0.95) - rmsDb(in, 0.6, 0.95);
        };

        // Reference at 40 Hz / 12 kHz, full knob:
        //   Fat +5.9 / +4.1   Warm +5.2 / -3.6   Bright -0.8 / +6.5
        const double fatLow  = probe(0.0f, 40.0),  fatHigh  = probe(0.0f, 12000.0);
        const double warmLow = probe(0.5f, 40.0),  warmHigh = probe(0.5f, 12000.0);
        const double briLow  = probe(1.0f, 40.0),  briHigh  = probe(1.0f, 12000.0);
        char buf[160];

        std::snprintf(buf, sizeof buf, "40Hz=%+.1f 12k=%+.1f", fatLow, fatHigh);
        check("Fat boosts both ends", fatLow > 3.0 && fatHigh > 2.0, buf);
        std::snprintf(buf, sizeof buf, "40Hz=%+.1f 12k=%+.1f", warmLow, warmHigh);
        check("Warm boosts lows, cuts highs", warmLow > 3.0 && warmHigh < -1.5, buf);
        std::snprintf(buf, sizeof buf, "40Hz=%+.1f 12k=%+.1f", briLow, briHigh);
        check("Bright cuts lows, boosts highs", briLow < 1.0 && briHigh > 4.0, buf);
    }

    void testAutomationSmoothing()
    {
        std::printf("\nAutomation is smoothed, and independent of the host block size\n");
        // Probe with a 100 Hz tone: its own sample-to-sample curvature is 100x
        // smaller than a 1 kHz tone's, so a coefficient step at a block
        // boundary stands out instead of hiding under the waveform.
        const int blockSize = 512;
        auto tone = sine(100.0, -20.0, (double)(blockSize * 60) / kSR);

        // Saturation is swept over Moderate's steep-onset region (§3.3) rather
        // than to the top of Hot: at full Hot drive the output is nearly a
        // square wave, whose own edges swamp any coefficient step.
        struct Sweep { const char* name; int param; float mode; double to; };
        const Sweep sweeps[] = {
            { "Compression 1 -> 10",  FCns::kParamCompression, 0.0f, 1.0 },
            { "Saturation 1 -> 5.5",  FCns::kParamSaturation,  0.5f, 0.5 },  // Moderate
        };
        for (const auto& s : sweeps) {
            auto dsp = makeDsp();
            setNeutral(dsp);
            dsp.setParameter(FCns::kParamSatMode, s.mode);
            auto out = renderChunked(dsp, tone, blockSize, s.param, 0.0, s.to);
            const double ratio = boundaryDiscontinuity(out.l, blockSize);
            char buf[160];
            std::snprintf(buf, sizeof buf,
                          "%s: boundary/interior 2nd difference %.1fx", s.name, ratio);
            // Unsmoothed this lands in the hundreds; smoothed it is ~1.
            check("block boundaries leave no step in the waveform", ratio < 3.0, buf);
        }

        {   // The sample loop now runs in control blocks. Chopping the stream
            // differently must not change a single bit of the result.
            auto steady = offsetSine(997.0, -20.0, 0.25);
            auto whole = makeDsp();   setNeutral(whole);
            auto split = makeDsp();   setNeutral(split);
            for (auto* d : { &whole, &split }) {
                d->setParameter(FCns::kParamCompression, 0.7f);
                d->setParameter(FCns::kParamSaturation, 0.6f);
                d->setParameter(FCns::kParamCharacter, 0.8f);
            }
            const int odd = 7;   // deliberately coprime with the control block
            auto a = renderChunked(whole, steady, (int)steady.size(), -1, 0.0, 0.0);
            auto b = renderChunked(split, steady, odd, -1, 0.0, 0.0);
            const double worst = maxAbsDiff(a.l, b.l);
            char buf[128];
            std::snprintf(buf, sizeof buf, "%zu samples in one block vs. blocks of %d, "
                          "max deviation %.3e", steady.size(), odd, worst);
            check("chopping the stream differently changes nothing, bit for bit",
                  worst == 0.0, buf);
        }

        {   // A glide must land exactly on its target, not merely approach it:
            // updateFilters caches on equality, so a smoother that only ever
            // converges asymptotically would redesign forever.
            // Mix=0 reduces the whole chain to one output-trim multiply, which
            // makes the settled gain directly observable and memoryless.
            const double settleSec = 0.35;
            auto tone1k = offsetSine(997.0, -20.0, settleSec);

            auto ref = makeDsp();  setNeutral(ref);
            ref.setParameter(FCns::kParamMix, 0.0f);
            ref.setParameter(FCns::kParamOutput, 1.0f);
            auto want = renderChunked(ref, tone1k, 512, -1, 0.0, 0.0);

            // One block at 0 dB parks the smoother there; then jump to +20 dB
            // and give it settleSec to arrive.
            auto glided = makeDsp();  setNeutral(glided);
            glided.setParameter(FCns::kParamMix, 0.0f);
            auto park = offsetSine(997.0, -20.0, 0.01);
            (void)renderChunked(glided, park, 512, -1, 0.0, 0.0);
            glided.setParameter(FCns::kParamOutput, 1.0f);
            auto got = renderChunked(glided, tone1k, 512, -1, 0.0, 0.0);

            double worst = 0.0;
            for (size_t i = want.l.size() - 100; i < want.l.size(); ++i)
                worst = std::max(worst, std::fabs(got.l[i] - want.l[i]));
            char buf[128];
            std::snprintf(buf, sizeof buf, "max deviation %.3e after %.0f ms",
                          worst, settleSec * 1000.0);
            check("a glided parameter settles exactly onto its target",
                  worst == 0.0, buf);
        }
    }

    void testResetClearsState()
    {
        std::printf("\nreset() clears envelopes and filter memory\n");
        auto configure = [](FermentCharge& d) {
            setNeutral(d);
            d.setParameter(FCns::kParamCompression, 1.0f);
            d.setParameter(FCns::kParamCharacter, 1.0f);
            d.setParameter(FCns::kParamDetectorHP, 1.0f);
        };
        auto loud = offsetSine(100.0, 0.0, 0.5);
        auto quiet = offsetSine(997.0, -40.0, 0.5);

        {   // Transport stop/start: the host calls reset(), then plays again.
            auto fresh = makeDsp();  configure(fresh);
            auto want = render(fresh, quiet, quiet);

            auto used = makeDsp();  configure(used);
            (void)render(used, loud, loud);
            used.reset();
            auto got = render(used, quiet, quiet);

            const double worst = maxAbsDiff(got.l, want.l);
            char buf[128];
            std::snprintf(buf, sizeof buf, "max deviation %.3e", worst);
            check("after reset a used instance matches a fresh one exactly",
                  worst == 0.0, buf);
        }

        {   // Sample-rate change: filters are redesigned for the new rate, but
            // their memory and the envelopes belong to the old one.
            const double sr2 = 96000.0;
            std::vector<double> quiet2((int)(0.25 * sr2));
            for (size_t i = 0; i < quiet2.size(); ++i)
                quiet2[i] = 0.01 * std::sin(2.0 * M_PI * 997.0 * (double)i / sr2 + 0.3);

            FermentCharge fresh(0);  fresh.setSampleRate((float)sr2);  configure(fresh);
            auto want = render(fresh, quiet2, quiet2);

            auto used = makeDsp();  configure(used);
            (void)render(used, loud, loud);
            used.setSampleRate((float)sr2);
            used.reset();
            auto got = render(used, quiet2, quiet2);

            const double worst = maxAbsDiff(got.l, want.l);
            char buf[128];
            std::snprintf(buf, sizeof buf, "max deviation %.3e", worst);
            check("after a 48k -> 96k switch plus reset, likewise", worst == 0.0, buf);
        }
    }

    // The three tests below were added after mutation testing
    // (tools/mutate_charge.py) found the suite stayed green while the DSP was
    // deliberately broken in these three ways. Each one is verified to fail
    // under its corresponding mutation.

    void testGainCellStaysSymmetric()
    {
        std::printf("\nGain cell stays symmetric under compression\n");
        // Mutation caught: adding any even-order term to the gain cell.
        // The reference measures H2 at -69.6 dB with H3 at -44.4 dB at full
        // compression (design doc 3.2) — H2 must stay down near the floor, not
        // merely below H3, or we have reintroduced the asymmetric "tube" gain
        // cell that the measurements ruled out.
        for (float comp : { 0.5f, 1.0f }) {
            auto dsp = makeDsp();
            setNeutral(dsp);
            dsp.setParameter(FCns::kParamCompression, comp);
            auto in = sine(220.0, -12.0, 1.0);
            auto out = render(dsp, in, in);
            const double h2 = harmonicDb(out.l, 220.0, 2);
            char buf[128];
            std::snprintf(buf, sizeof buf, "comp=%.1f H2=%.1f dB", comp, h2);
            check("2nd harmonic stays below -60 dB (cell is symmetric)",
                  h2 < -60.0, buf);
        }
    }

    void testDetectorHighPass()
    {
        std::printf("\nDetector high-pass changes what triggers the compressor\n");
        // Mutation caught: removing the detector high-pass entirely.
        // Reference at 30 Hz (design doc 3.5): Off gives -4.7 dB of gain
        // reduction, the 300 Hz setting gives +6.5 dB — an 11 dB swing,
        // because filtering bass out of the detector stops it triggering.
        auto gainAt = [&](double freq, float hpMode) {
            auto dsp = makeDsp();
            setNeutral(dsp);
            dsp.setParameter(FCns::kParamCompression, 0.7f);
            dsp.setParameter(FCns::kParamDetectorHP, hpMode);
            auto in = sine(freq, -8.0, 1.2);
            auto out = render(dsp, in, in);
            return rmsDb(out.l, 0.6, 0.95) - rmsDb(in, 0.6, 0.95);
        };

        const double lowOff = gainAt(30.0, 0.0f);
        const double low300 = gainAt(30.0, 1.0f);
        const double highOff = gainAt(1500.0, 0.0f);
        const double high300 = gainAt(1500.0, 1.0f);
        char buf[192];

        std::snprintf(buf, sizeof buf, "30 Hz: Off=%+.2f, 300Hz=%+.2f (swing %.2f)",
                      lowOff, low300, low300 - lowOff);
        check("300 Hz setting greatly reduces bass triggering",
              low300 - lowOff > 4.0, buf);

        // Well above the filter's corner the response is flat, so a naive
        // implementation would leave gain reduction unchanged. The reference
        // instead compresses ~2.9 dB *harder* at 1.5 kHz with the 300 Hz
        // setting engaged (-6.2 -> -9.1 dB), because removing bass from its
        // detector makes its feedback loop drive further. This port supplies
        // that explicitly; see kDetectorMakeupDb in FermentChargeProc.cpp.
        std::snprintf(buf, sizeof buf, "1500 Hz: Off=%+.2f, 300Hz=%+.2f (deeper by %.2f)",
                      highOff, high300, highOff - high300);
        check("engaging the filter deepens gain reduction above its corner",
              highOff - high300 > 1.5, buf);
    }

    void testChannelSymmetry()
    {
        std::printf("\nIdentical channels stay identical\n");
        // Mutation caught: any one-sided bug. Every other test in this file
        // reads out.l only, so a right-channel-only regression was invisible.
        // Exact equality is not required: the Airwindows denormal guard seeds
        // L and R from different PRNG states, injecting ~5e-8 at zero
        // crossings. That is -145 dBFS and inaudible, but it is not zero.
        struct Setting { const char* name; int param; float value; };
        const Setting settings[] = {
            { "compression", FCns::kParamCompression, 1.0f },
            { "saturation",  FCns::kParamSaturation,  1.0f },
            { "character",   FCns::kParamCharacter,   1.0f },
            { "output trim", FCns::kParamOutput,      1.0f },
            { "input trim",  FCns::kParamInput,       1.0f },
        };

        for (const auto& s : settings) {
            auto dsp = makeDsp();
            setNeutral(dsp);
            dsp.setParameter(s.param, s.value);
            auto in = sine(440.0, -10.0, 0.6);
            auto out = render(dsp, in, in);

            double worst = 0.0, peak = 0.0;
            for (size_t i = 0; i < out.l.size(); ++i) {
                worst = std::max(worst, std::fabs(out.l[i] - out.r[i]));
                peak  = std::max(peak, std::fabs(out.l[i]));
            }
            // Relative, not absolute: the output dither is magnitude-
            // proportional (scaled by 2^expon) and decorrelated per channel
            // on purpose, so the absolute gap tracks signal level. -100 dB
            // relative is far below the dither floor yet still catches any
            // real one-sided regression, which is orders of magnitude larger.
            const double rel = worst / (peak + 1e-30);
            const double rmsGap = std::fabs(rmsDb(out.l) - rmsDb(out.r));

            char buf[160];
            std::snprintf(buf, sizeof buf, "%s: gap %.2e (%.1f dB rel), RMS gap %.4f dB",
                          s.name, worst, 20.0 * std::log10(rel + 1e-30), rmsGap);
            check("L and R match", rel < 1e-5 && rmsGap < 0.01, buf);
        }
    }

    void testMixAndHygiene()
    {
        std::printf("\nMix, trim and numerical hygiene\n");
        auto in = sine(1000.0, -12.0, 0.5);

        {   // Mix at 0 must be bit-clean dry (aside from trim, which is unity).
            auto dsp = makeDsp();
            setNeutral(dsp);
            dsp.setParameter(FCns::kParamCompression, 1.0f);
            dsp.setParameter(FCns::kParamSaturation, 1.0f);
            dsp.setParameter(FCns::kParamMix, 0.0f);
            auto out = render(dsp, in, in);
            double worst = 0.0;
            for (size_t i = 0; i < in.size(); ++i)
                worst = std::max(worst, std::fabs(out.l[i] - in[i]));
            char buf[96];
            std::snprintf(buf, sizeof buf, "max deviation %.3e", worst);
            check("Mix=0 passes dry signal through unchanged", worst < 1e-9, buf);
        }

        {   // Output trim is exactly +/-20 dB at the extremes.
            auto dsp = makeDsp();
            setNeutral(dsp);
            dsp.setParameter(FCns::kParamMix, 0.0f);
            dsp.setParameter(FCns::kParamOutput, 1.0f);
            auto out = render(dsp, in, in);
            checkNear("Output at max is +20 dB",
                      rmsDb(out.l, 0.5, 1.0) - rmsDb(in, 0.5, 1.0), 20.0, 0.05);
        }

        {   // Silence in must not produce NaN, inf or runaway makeup.
            auto dsp = makeDsp();
            setNeutral(dsp);
            dsp.setParameter(FCns::kParamCompression, 1.0f);
            dsp.setParameter(FCns::kParamSaturation, 1.0f);
            dsp.setParameter(FCns::kParamCharacter, 1.0f);
            std::vector<double> zeros(4096, 0.0);
            auto out = render(dsp, zeros, zeros);
            bool clean = true;
            for (double v : out.l)
                if (!std::isfinite(v) || std::fabs(v) > 1e-3) clean = false;
            check("silence stays finite and quiet at extreme settings", clean);
        }

        {   // Full-scale input at max everything must stay bounded.
            auto hot = sine(1000.0, 0.0, 0.5);
            auto dsp = makeDsp();
            setNeutral(dsp);
            dsp.setParameter(FCns::kParamCompression, 1.0f);
            dsp.setParameter(FCns::kParamSaturation, 1.0f);
            dsp.setParameter(FCns::kParamSatMode, 1.0f);
            dsp.setParameter(FCns::kParamCharacter, 1.0f);
            dsp.setParameter(FCns::kParamInput, 1.0f);
            auto out = render(dsp, hot, hot);
            bool bounded = true;
            for (double v : out.l)
                if (!std::isfinite(v) || std::fabs(v) > 8.0) bounded = false;
            check("hot input at extreme settings stays finite and bounded", bounded);
        }
    }
}

int main()
{
    std::printf("Ferment Charge DSP tests (SR=%.0f Hz)\n", kSR);
    testStaticCurve();
    testNoThreshold();
    testSymmetricGainCell();
    testBallistics();
    testStereoModes();
    testSaturationModes();
    testCharacterModes();
    testAutomationSmoothing();
    testResetClearsState();
    testGainCellStaysSymmetric();
    testDetectorHighPass();
    testChannelSymmetry();
    testMixAndHygiene();
    std::printf("\n%s\n", fails == 0 ? "ALL CHARGE TESTS PASSED"
                                     : "CHARGE TESTS FAILED");
    return fails == 0 ? 0 : 1;
}

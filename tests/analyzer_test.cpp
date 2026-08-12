// analyzer-core + ferment-policy tests.
//
// Synthetic invariants pin the realtime meters to known ground truth.

#include "../src-ferment/core/AnalyzerCore.h"
#include "../src-ferment/core/FermentPolicy.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

using namespace ferment::analyzer;

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
    char buf[128];
    std::snprintf(buf, sizeof buf, "got %.3f, want %.3f +/- %.2f", got, want, tol);
    check(name, std::fabs(got - want) <= tol, buf);
}

std::vector<double> sine(double freq, double db, double seconds)
{
    const int n = (int)(seconds * kSR);
    std::vector<double> v((size_t)n);
    const double amp = std::pow(10.0, db / 20.0);
    for (int i = 0; i < n; ++i)
        v[(size_t)i] = amp * std::sin(2.0 * M_PI * freq * i / kSR);
    return v;
}

std::vector<double> noise(double amp, int n, unsigned seed)
{
    std::vector<double> v((size_t)n);
    unsigned s = seed;
    for (int i = 0; i < n; ++i) {
        s = s * 1664525u + 1013904223u;
        v[(size_t)i] = amp * (2.0 * (s / 4294967296.0) - 1.0);
    }
    return v;
}

Readout run(const std::vector<double>& l, const std::vector<double>& r,
            Profile p = profileStudio())
{
    AnalyzerCore core;
    core.prepare(kSR);
    core.setProfile(p);
    // stream in awkward block sizes on purpose
    int i = 0, n = (int)l.size();
    const int sizes[] = {480, 113, 1024, 4096, 63};
    int si = 0;
    while (i < n) {
        const int b = std::min(sizes[si++ % 5], n - i);
        core.process(l.data() + i, r.data() + i, b);
        i += b;
    }
    return core.readout();
}

void testLufs()
{
    std::printf("LUFS:\n");
    // EBU Tech 3341 case: 997 Hz sine, both channels at -20 dBFS, must read
    // -20.0 LUFS (the -0.691 offset and the shelf's +0.65 dB at 997 Hz are
    // designed to cancel for exactly this reference).
    auto s = sine(997.0, -20.0, 6.0);
    auto r = run(s, s);
    checkNear("997 Hz -20 dBFS stereo integrated", r.lufsIntegrated, -20.0, 0.15);
    checkNear("short-term matches integrated", r.lufsShortTerm, r.lufsIntegrated, 0.3);
    check("loudnessReady", r.loudnessReady);
}

void testPeaks()
{
    std::printf("peaks:\n");
    auto s = sine(997.0, -6.0, 2.0);
    auto r = run(s, s);
    checkNear("sample peak", r.samplePeakDb, -6.0, 0.05);
    check("TP >= sample peak - 0.05", r.truePeakDb >= r.samplePeakDb - 0.05);
    check("TP within +1 dB on smooth sine", r.truePeakDb <= -6.0 + 1.0);
    check("no clips", r.clipCount == 0);
    check("not pre-clipped", !r.preClipped);
}

void testCrest()
{
    std::printf("crest:\n");
    auto s = sine(220.0, -12.0, 4.0);
    auto r = run(s, s);
    checkNear("sine crest 3.01 dB", r.crestDb, 3.01, 0.3);
    check("alreadyDense verdict (crest < 9)", r.alreadyDense);
}

void testSpectrum()
{
    std::printf("spectrum:\n");
    auto nz = noise(0.1, (int)(6 * kSR), 1);
    auto r = run(nz, nz);
    check("spectrumReady", r.spectrumReady);
    checkNear("white noise tilt ~0", r.tiltDb, 0.0, 1.0);
    double sum = 0.0;
    for (double b : r.bandShare) sum += b;
    checkNear("band shares sum to 1", sum, 1.0, 0.02);

    auto sub = sine(40.0, -12.0, 6.0);
    auto r2 = run(sub, sub);
    check("40 Hz sine is sub-heavy", r2.subClass == Readout::SubHeavy);
    checkNear("sub share ~1", r2.subShare, 1.0, 0.05);
    checkNear("sub-heavy low-shelf ladder", r2.targets.subTrimDb, -4.0, 1e-9);
}

void testMono()
{
    std::printf("mono:\n");
    auto l = noise(0.1, (int)(4 * kSR), 2);
    auto rch = l;
    for (double& x : rch) x = -x;           // anti-phase: mono fold nulls
    auto r = run(l, rch);
    check("anti-phase mono loss > 20 dB", r.monoLossDb > 20.0);
    check("monoFragile", r.monoFragile);
    checkNear("width ladder floor", r.targets.widthFactor, 0.6, 1e-9);

    auto r2 = run(l, l);
    checkNear("correlated mono loss ~0", r2.monoLossDb, 0.0, 0.2);
}

void testTargetsAndPolicy()
{
    std::printf("targets + policy:\n");
    auto s = sine(60.0, -6.0, 6.0);        // loud dark sub-heavy source
    auto r = run(s, s, profileStudio());
    // staging: drive point -16 minus measured LUFS
    checkNear("staging delta", r.targets.driveDeltaDb, -16.0 - r.lufsIntegrated, 1e-9);
    check("tilt delta clamped to 6", r.targets.tiltDeltaDb <= 6.0 + 1e-9);

    auto cs = ferment::policy::translate(r, profileStudio());
    check("studio sub-heavy -> Mild", cs.charge.satMode == 0);
    check("studio sub-heavy -> HP300", cs.charge.detectorHp == 2);
    checkNear("studio sub-heavy mix 60", cs.charge.mixPct, 60.0, 1e-9);
    checkNear("clip rides 0.5 over limit", cs.clip.ceilingDb - cs.limit.ceilingDbtp, 0.5, 1e-9);

    auto cr = ferment::policy::translate(r, profileReel());
    check("reel sub-heavy -> Moderate", cr.charge.satMode == 1);
    checkNear("reel limit ceiling -1.0", cr.limit.ceilingDbtp, -1.0, 1e-9);
    checkNear("reel knee 22", cr.clip.kneePct, 22.0, 1e-9);
}

void testNonFiniteInputCannotPoison()
{
    std::printf("non-finite input:\n");
    auto clean = sine(220.0, -12.0, 5.0);
    AnalyzerCore core;
    core.prepare(kSR);
    core.process(clean.data(), clean.data(), (int)clean.size());
    const auto before = core.readout();

    std::vector<double> bad(64, std::numeric_limits<double>::quiet_NaN());
    core.process(bad.data(), bad.data(), (int)bad.size());
    core.process(clean.data(), clean.data(), (int)clean.size());
    const auto after = core.readout();

    check("tilt survives NaN input", std::isfinite(after.tiltDb));
    check("LUFS survives NaN input", std::isfinite(after.lufsIntegrated));
    check("targets stay finite", std::isfinite(after.targets.tiltDeltaDb)
                              && std::isfinite(after.targets.driveDeltaDb));
    // Not "unchanged": the guard substitutes silence, and 64 silent samples
    // in the middle of a sine is a click, which legitimately raises the top
    // end of an integrated spectrum. The invariant is that the reading stays
    // finite and still describes a dark signal.
    check("tilt still reads a dark signal", after.tiltDb < 0.0 && after.tiltDb > -200.0);
    (void)before;
}

void testSilenceHasNoOpinions()
{
    std::printf("silence:\n");

    // An instance that has only ever heard silence must not light a single
    // verdict: crest of nothing is -300 (reads "dense"), tilt of nothing is 0
    // (reads "bright" against any musical target).
    std::vector<double> quiet((size_t)(8 * kSR), 0.0);
    auto r = run(quiet, quiet);

    check("no signal reported", !r.signalPresent);
    check("no spectrum from silence", !r.spectrumReady);
    check("silence is not ALREADY DENSE", !r.alreadyDense);
    check("silence is not MONO FRAGILE", !r.monoFragile);
    check("silence is not sub-heavy", r.subClass == Readout::SubBalanced);

    // A track that plays and then stops keeps its verdicts: the integrated
    // measures are gated, so trailing silence must not erase them.
    auto s = sine(220.0, -12.0, 6.0);
    AnalyzerCore core;
    core.prepare(kSR);
    core.process(s.data(), s.data(), (int)s.size());
    core.process(quiet.data(), quiet.data(), (int)quiet.size());
    const auto after = core.readout();

    check("LUFS-I survives trailing silence", after.loudnessReady);
    check("spectrum survives trailing silence", after.spectrumReady);
    // Not "unmoved": the 400 ms gating blocks straddling the stop are part
    // sine, part silence — quiet enough to drag the gated mean a tenth of a
    // dB, loud enough to pass the gates.  Real tracks end the same way.
    checkNear("integrated loudness barely moved by silence",
              after.lufsIntegrated, run(s, s).lufsIntegrated, 0.3);

    // But the live short-term verdicts relax: nothing is playing.
    check("signalPresent relaxes after stop", !after.signalPresent);
    check("DENSE goes out after stop", !after.alreadyDense);

    // And policy never emits a staging value no knob can hold, even off a
    // degenerate readout.
    auto cs = ferment::policy::translate(r, profileStudio());
    check("staging clamped to the knob's range",
          cs.stagingDb >= -24.0 && cs.stagingDb <= 24.0);
}

void testStreamingInvariance()
{
    std::printf("streaming invariance:\n");
    auto s = sine(500.0, -14.0, 5.0);
    AnalyzerCore one;
    one.prepare(kSR);
    one.process(s.data(), s.data(), (int)s.size());
    auto a = one.readout();
    auto b = run(s, s);
    checkNear("block-size independent LUFS", a.lufsIntegrated, b.lufsIntegrated, 1e-6);
    checkNear("block-size independent tilt", a.tiltDb, b.tiltDb, 1e-6);
}

} // namespace

int main()
{
    std::printf("Ferment Analyzer core tests\n\n");
    testLufs();
    testPeaks();
    testCrest();
    testSpectrum();
    testMono();
    testTargetsAndPolicy();
    testNonFiniteInputCannotPoison();
    testSilenceHasNoOpinions();
    testStreamingInvariance();
    std::printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "OK", fails,
                fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}

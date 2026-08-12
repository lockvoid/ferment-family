// MasterCore tests: chain correctness, Learn end-to-end, bypass hygiene.

#include "../src-ferment/core/MasterCore.h"
#include "../src-ferment/core/AnalyzerCore.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using namespace ferment::master;
using ferment::analyzer::AnalyzerCore;
using ferment::analyzer::profileStudio;
using ferment::analyzer::profileReel;

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

// Sub-heavy dark fixture: 45 Hz body + quiet mids + hits, deterministic.
void fixture(std::vector<double>& l, std::vector<double>& r, double seconds)
{
    const int n = (int)(seconds * kSR);
    l.assign((size_t)n, 0.0);
    r.assign((size_t)n, 0.0);
    unsigned s = 42;
    double lp = 0.0;   // dark broadband floor: the tilt metric averages dB
                       // per bin, so darkness must be broadband, not tonal
    for (int i = 0; i < n; ++i) {
        const double t = i / kSR;
        s = s * 1664525u + 1013904223u;
        const double nz = 2.0 * (s / 4294967296.0) - 1.0;
        lp += 0.06 * (nz - lp);
        double x = 0.35 * std::sin(2.0 * M_PI * 45.0 * t)
                 + 0.05 * std::sin(2.0 * M_PI * 300.0 * t)
                 + 0.04 * lp;
        // a kick-ish transient every 500 ms
        const double ph = std::fmod(t, 0.5);
        if (ph < 0.03) x += 0.4 * std::exp(-ph * 200.0) * std::sin(2.0 * M_PI * 70.0 * ph * 30.0);
        l[(size_t)i] = x;
        r[(size_t)i] = x * 0.98;
    }
}

void testPassthroughWhenAllBypassed()
{
    std::printf("bypass = latency-matched passthrough:\n");
    MasterCore core;
    core.prepare(kSR, 1024);
    for (int m = 0; m < kNumModules; ++m) core.setBypass((Module)m, true);
    core.reset();
    for (int m = 0; m < kNumModules; ++m) core.setBypass((Module)m, true);

    std::vector<double> l, r;
    fixture(l, r, 2.0);
    std::vector<double> refL = l, refR = r;
    core.process(l.data(), r.data(), (int)l.size());

    const int lat = core.latencySamples();
    double err = 0.0;
    for (size_t i = (size_t)lat + 48000; i < l.size(); ++i)
        err = std::max(err, std::fabs(l[i] - refL[i - (size_t)lat]));
    char buf[96];
    std::snprintf(buf, sizeof buf, "max err %.2e at latency %d", err, lat);
    check("bypassed chain == delayed input", err < 1e-9, buf);
}

void testLearnEndToEnd()
{
    std::printf("Learn end-to-end (studio, sub-heavy fixture):\n");
    MasterCore core;
    core.prepare(kSR, 4096);
    core.setProfile(profileStudio());
    core.reset();

    std::vector<double> l, r;
    fixture(l, r, 12.0);

    core.learnStart();
    core.process(l.data(), r.data(), (int)l.size());
    check("phase 1 active through playback", core.phase() == 1);

    const auto s = core.learnFinish();
    check("sub-heavy -> Mild", s.charge.satMode == 0);
    check("sub-heavy -> HP300", s.charge.detectorHp == 2);
    checkNear("mix 60", s.charge.mixPct, 60.0, 1e-9);
    checkNear("low shelf ladder", s.eq.lowShelfDb, -4.0, 1e-9);
    check("tilt shelf strongly engaged (dark fixture)",
          s.eq.highShelfDb > 3.0 && s.eq.highShelfDb <= 6.0 + 1e-9);

    // phase 2: keep playing; push lands automatically after ~4 s
    std::vector<double> l2, r2;
    fixture(l2, r2, 6.0);
    core.process(l2.data(), r2.data(), (int)l2.size());
    check("phase 2 completed", !core.learning());
    check("push applied", std::fabs(core.appliedPushDb()) > 0.5);

    // fresh render through the learned chain: does it land near target?
    MasterCore verify;
    verify.prepare(kSR, 4096);
    verify.setProfile(profileStudio());
    verify.reset();
    verify.applySettings(s);
    // apply the same learned push by re-running the two-phase learn is the
    // real path; here we clone the applied state via a second learn pass
    // to keep the test to public API:
    verify.learnStart();
    std::vector<double> l3, r3;
    fixture(l3, r3, 12.0);
    verify.process(l3.data(), r3.data(), (int)l3.size());
    verify.learnFinish();
    fixture(l3, r3, 6.0);
    verify.process(l3.data(), r3.data(), (int)l3.size());

    verify.reset();  // clear meters/state, keep settings+push
    std::vector<double> l4, r4;
    fixture(l4, r4, 12.0);
    verify.process(l4.data(), r4.data(), (int)l4.size());
    const auto res = verify.resultReadout();
    checkNear("result lands near -9.5 LUFS", res.lufsIntegrated, -9.5, 1.0);
    check("result TP under ceiling+0.3", res.truePeakDb <= -1.5 + 0.35);
}

void testSetPushDb()
{
    std::printf("setPushDb (the reload path):\n");
    // The wrapper drives this from the restored push_db parameter; +6 dB of
    // push on a signal below every ceiling is exactly +6 dB of output.
    auto rmsOut = [](double pushDb) {
        MasterCore core;
        core.prepare(kSR, 1024);
        core.reset();
        core.setPushDb(pushDb);
        std::vector<double> l((size_t)(2.0 * kSR)), r;
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = 0.02 * std::sin(2.0 * M_PI * 220.0 * (double)i / kSR);
        r = l;
        core.process(l.data(), r.data(), (int)l.size());
        double acc = 0.0;
        const size_t tail = l.size() / 2;
        for (size_t i = tail; i < l.size(); ++i) acc += l[i] * l[i];
        return 10.0 * std::log10(acc / (double)tail + 1e-30);
    };
    checkNear("push +6 = +6 dB of output", rmsOut(6.0) - rmsOut(0.0), 6.0, 0.05);

    MasterCore clampCheck;
    clampCheck.prepare(kSR, 1024);
    clampCheck.setPushDb(100.0);
    checkNear("push clamps to the knob", clampCheck.appliedPushDb(), 24.0, 1e-12);
}

void testLearnOnSilenceIsANoOp()
{
    std::printf("Learn on silence:\n");
    MasterCore core;
    core.prepare(kSR, 4096);
    core.setProfile(profileStudio());
    core.reset();

    const auto before = core.settings();

    std::vector<double> quiet((size_t)(12 * kSR), 0.0);
    core.learnStart();
    core.process(quiet.data(), quiet.data(), (int)quiet.size());
    const auto s = core.learnFinish();

    // A Learn that heard nothing learns nothing: no phase 2, settings kept,
    // no push — instead of staging pinned at the clamp and a +24 push.
    check("no phase 2 on silence", core.phase() == 0);
    checkNear("staging unchanged", s.stagingDb, before.stagingDb, 1e-12);
    checkNear("no push from silence", core.appliedPushDb(), 0.0, 1e-12);

    /*  Transport stop right after phase 1: the chain's decay tail rings above
        the gate for a moment, then silence.  Counting that window used to set
        the push from the tail — measured -40, push +24, the blast on the next
        play.  Phase 2 must instead freeze on silence and finish only once it
        has heard four real seconds.  */
    std::vector<double> l, r;
    fixture(l, r, 12.0);
    core.learnStart();
    core.process(l.data(), r.data(), (int)l.size());
    core.learnFinish();
    std::fill(quiet.begin(), quiet.end(), 0.0);      // dirtied in-place above
    core.process(quiet.data(), quiet.data(), (int)(6 * kSR));
    check("phase 2 waits through silence", core.phase() == 2);
    checkNear("no push measured off the decay tail", core.appliedPushDb(), 0.0, 1e-12);

    // Playback resumes: phase 2 completes on the real audio it was waiting
    // for, and the push comes from the music, not the tail.
    fixture(l, r, 6.0);
    core.process(l.data(), r.data(), (int)l.size());
    char buf[64];
    std::snprintf(buf, sizeof buf, "push %.2f dB", core.appliedPushDb());
    check("phase 2 completes when audio returns", core.phase() == 0);
    check("push is sane, not the +24 clamp",
          core.appliedPushDb() > 0.0 && core.appliedPushDb() < 20.0, buf);
}

void testDeterminism()
{
    std::printf("determinism:\n");
    auto runLearn = [](double& push, double& shelf) {
        MasterCore core;
        core.prepare(kSR, 4096);
        core.setProfile(profileReel());
        core.reset();
        std::vector<double> l, r;
        fixture(l, r, 12.0);
        core.learnStart();
        core.process(l.data(), r.data(), (int)l.size());
        auto s = core.learnFinish();
        fixture(l, r, 6.0);
        core.process(l.data(), r.data(), (int)l.size());
        push = core.appliedPushDb();
        shelf = s.eq.highShelfDb;
    };
    double p1, s1, p2, s2;
    runLearn(p1, s1);
    runLearn(p2, s2);
    checkNear("identical push across runs", p1, p2, 1e-12);
    checkNear("identical shelf across runs", s1, s2, 1e-12);
}

} // namespace

int main()
{
    std::printf("Ferment MasterCore tests\n\n");
    testPassthroughWhenAllBypassed();
    testLearnEndToEnd();
    testSetPushDb();
    testLearnOnSilenceIsANoOp();
    testDeterminism();
    std::printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "OK", fails,
                fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}

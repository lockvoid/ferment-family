/* ========================================
 *  Ferment Master — MasterCore (logic)
 *
 *  The mastering chain as one streaming core: the family's pure DSP
 *  engines wired in the validated order, analyzer-core taps before and
 *  after, and one-shot Learn = analyze -> ferment-policy -> apply.
 *
 *  Pure C++ (no JUCE). The plugin wrapper later maps this onto APVTS;
 *  Learn writes ordinary visible parameters, never hidden state.
 *
 * ======================================== */

#ifndef FERMENT_MASTER_CORE_H
#define FERMENT_MASTER_CORE_H

#include "../core/AnalyzerCore.h"
#include "../core/FermentPolicy.h"
#include "../common/Biquad.h"

#include <memory>
#include <vector>

namespace airwinconsolidated::FermentCharge { class FermentCharge; }
namespace airwinconsolidated::FermentClip   { class FermentClip; }
namespace airwinconsolidated::FermentLimit  { class FermentLimit; }

namespace ferment::master {

enum Module { ModStage = 0, ModCharge, ModTone, ModClip, ModLimit, kNumModules };

class MasterCore {
public:
    MasterCore();
    ~MasterCore();

    void prepare(double sampleRate, int maxBlock);
    void reset();

    void setProfile(const ferment::analyzer::Profile& p);
    const ferment::analyzer::Profile& profile() const;

    // --- Learn: two-phase, deterministic --------------------------------
    // Phase 1: learnStart() -> stream the loud section -> learnFinish()
    //   translates the SOURCE readout via ferment-policy and applies the
    //   colour modules (stage/charge/tone/clip curve/limit voicing).
    // Phase 2 (automatic): the core keeps analysing at the post-Tone tap
    //   for ~4 s of further playback, then sets the loudness push at the
    //   Clip input = profile target - measured post-colour loudness (the
    //   measured gain to target). learning() turns false when done.
    void learnStart();
    bool learning() const { return learnPhase != 0; }
    int  phase() const { return learnPhase; }
    ferment::policy::ChainSettings learnFinish();
    double appliedPushDb() const { return pushDb; }
    /** Sets the loudness push directly — the wrapper drives this from the
        visible Push parameter, which is how a push learned in a saved session
        comes back on reload, and how a host automates it.  Phase 2 of a
        running Learn overwrites whatever is set here when it completes. */
    void setPushDb(double db);
    // Phase 2 auto-completes after this much audio (default 4 s). Offline
    // callers (CLI/tests) may extend it and call completePhase2() manually
    // to measure the push over a whole track instead of the 4 s window.
    void setPhase2AutoSeconds(double s) { phase2AutoSec = s; }
    void completePhase2();

    void applySettings(const ferment::policy::ChainSettings& s);
    const ferment::policy::ChainSettings& settings() const { return current; }

    void setBypass(Module m, bool bypassed);
    bool bypassed(Module m) const { return byp[m].target < 0.5; }

    // In-place stereo processing.
    void process(double* left, double* right, int numSamples);

    ferment::analyzer::Readout sourceReadout() const { return pre.readout(); }
    ferment::analyzer::Readout resultReadout() const { return post.readout(); }

    // Per-module meters for the UI (engine accessors passed through).
    double chargeGrDb() const;
    double clipShaveDb() const;
    double limitGrDb() const;

    int latencySamples() const;

private:
    double sr = 48000.0;
    int cap = 0;

    // engines
    std::unique_ptr<airwinconsolidated::FermentCharge::FermentCharge> charge;
    std::unique_ptr<airwinconsolidated::FermentClip::FermentClip> clip;
    std::unique_ptr<airwinconsolidated::FermentLimit::FermentLimit> limit;

    // Stage: staging gain + HP 24 Hz (4th-order butterworth as two RBJ
    // sections) + width.
    double stageGainLin = 1.0;
    double widthFactor = 1.0;
    ferment::Biquad hp1, hp2;

    // Tone: the tilt EQ (low shelf 100 / bell 3k / high shelf 7k).
    ferment::Biquad loShelf, presence, hiShelf;
    bool toneActive[3] = {false, false, false};

    // Per-module click-free bypass: latency-matched dry + 10 ms crossfade.
    struct Bypass {
        double target = 1.0, gain = 1.0, step = 0.0;
        std::vector<double> dl, dr;
        int idx = 0, delay = 0;
        void prepare(int delaySamples, int capacity);
        void reset();
    };
    Bypass byp[kNumModules];
    void runBypassMix(Bypass& b, const double* dryL, const double* dryR,
                      double* wetL, double* wetR, int n);

    // analyzer taps + learn accumulator (phase 1: source, phase 2: after
    // the Tone module — the right measurement point for the
    // loudness push)
    ferment::analyzer::AnalyzerCore pre, post, learn;
    int learnPhase = 0;             // 0 idle, 1 source, 2 post-colour
    long long phase2Samples = 0;
    double phase2AutoSec = 4.0;
    double pushDb = 0.0;

    ferment::policy::ChainSettings current;

    // scratch
    std::vector<double> tL, tR, silentA, silentB, dryL, dryR;

    void applyToEngines();
};

} // namespace ferment::master

#endif

/* ========================================
 *  Ferment Master — MasterCore implementation
 * ======================================== */

#include "MasterCore.h"

#include "../charge/FermentCharge.h"
#include "../clip/FermentClip.h"
#include "../limit/FermentLimit.h"

#include <algorithm>
#include <cmath>

namespace ferment::master {

namespace FCh = airwinconsolidated::FermentCharge;
namespace FCl = airwinconsolidated::FermentClip;
namespace FLm = airwinconsolidated::FermentLimit;

static inline double db2lin(double db) { return std::pow(10.0, db / 20.0); }

MasterCore::MasterCore()
{
    charge = std::make_unique<FCh::FermentCharge>(0);
    clip   = std::make_unique<FCl::FermentClip>(0);
    limit  = std::make_unique<FLm::FermentLimit>(0);
}

MasterCore::~MasterCore() = default;

void MasterCore::Bypass::prepare(int delaySamples, int capacity)
{
    delay = delaySamples;
    dl.assign((size_t)std::max(1, delaySamples), 0.0);
    dr.assign((size_t)std::max(1, delaySamples), 0.0);
    (void)capacity;
    reset();
}

void MasterCore::Bypass::reset()
{
    std::fill(dl.begin(), dl.end(), 0.0);
    std::fill(dr.begin(), dr.end(), 0.0);
    idx = 0;
    gain = target;
    step = 0.0;
}

void MasterCore::prepare(double sampleRate, int maxBlock)
{
    sr = sampleRate;
    cap = maxBlock;

    charge->setSampleRate((float)sr);
    clip->setSampleRate((float)sr);
    limit->setSampleRate((float)sr);

    // 4th-order butterworth HP at 24 Hz = two RBJ sections, Q 0.5412 / 1.3066
    hp1.setLowCut(24.0, 0.54119610, sr);
    hp2.setLowCut(24.0, 1.30656296, sr);

    byp[ModStage].prepare(0, cap);
    byp[ModCharge].prepare(0, cap);
    byp[ModTone].prepare(0, cap);
    byp[ModClip].prepare(FCl::kLatencySamples, cap);
    byp[ModLimit].prepare(FLm::FermentLimit::latencySamples(sr), cap);

    pre.prepare(sr);
    post.prepare(sr);
    learn.prepare(sr);

    tL.assign((size_t)cap, 0.0);
    tR.assign((size_t)cap, 0.0);
    silentA.assign((size_t)cap, 0.0);
    silentB.assign((size_t)cap, 0.0);
    dryL.assign((size_t)cap, 0.0);
    dryR.assign((size_t)cap, 0.0);

    reset();
}

void MasterCore::reset()
{
    charge->reset();
    clip->reset();
    limit->reset();
    hp1.reset(); hp2.reset();
    loShelf.reset(); presence.reset(); hiShelf.reset();
    for (auto& b : byp) b.reset();
    pre.reset();
    post.reset();
    learn.reset();
    learnPhase = 0;
    phase2Samples = 0;
    applyToEngines();
}

void MasterCore::setProfile(const ferment::analyzer::Profile& p)
{
    pre.setProfile(p);
    post.setProfile(p);
    learn.setProfile(p);
}

const ferment::analyzer::Profile& MasterCore::profile() const
{
    return pre.activeProfile();
}

// --- Learn ------------------------------------------------------------------

void MasterCore::learnStart()
{
    /*  pushDb is deliberately left alone.  Zeroing it here without applying
        it made the reported value and the audio disagree for the whole Learn
        — and worse, the zero sat armed until the next applySettings, so any
        knob touched after an aborted Learn dropped the level by the old push.
        The push keeps its value until phase 2 measures a new one.  */
    learn.reset();
    learnPhase = 1;
}

ferment::policy::ChainSettings MasterCore::learnFinish()
{
    const auto readout = learn.readout();

    /*  A Learn that heard nothing learns nothing.  Ten seconds of real audio
        makes loudnessReady true; silence (or a signal below the -70 gate)
        does not, and translating that readout would set the chain from the
        gate floor — staging pinned at the clamp, a +24 push in phase 2.
        Keep what we had and go back to idle; the face reads it as a Learn
        that ended.  */
    if (!readout.loudnessReady) {
        learn.reset();
        learnPhase = 0;
        return current;
    }

    const auto s = ferment::policy::translate(readout, learn.activeProfile());
    applySettings(s);            // colour modules; push stays 0 until phase 2
    learn.reset();
    phase2Samples = 0;
    learnPhase = 2;
    return s;
}

// --- settings ---------------------------------------------------------------

void MasterCore::applySettings(const ferment::policy::ChainSettings& s)
{
    current = s;
    applyToEngines();
}

void MasterCore::applyToEngines()
{
    const auto& s = current;

    stageGainLin = db2lin(s.stagingDb);
    widthFactor = s.widthFactor;

    using FCh::FermentCharge;
    charge->setParameter(FCh::kParamInput, 0.5f);
    charge->setParameter(FCh::kParamCompression, (float)FermentCharge::normFromKnob(s.charge.compression));
    charge->setParameter(FCh::kParamAttack,      (float)FermentCharge::normFromKnob(s.charge.attack));
    charge->setParameter(FCh::kParamRelease,     (float)FermentCharge::normFromKnob(s.charge.release));
    charge->setParameter(FCh::kParamSaturation,  (float)FermentCharge::normFromKnob(s.charge.saturation));
    charge->setParameter(FCh::kParamSatMode,     (float)(s.charge.satMode * 0.5));
    charge->setParameter(FCh::kParamCharacter,   (float)FermentCharge::normFromKnob(s.charge.character));
    charge->setParameter(FCh::kParamCharMode,    (float)(s.charge.charMode * 0.5));
    charge->setParameter(FCh::kParamDetectorHP,  (float)(s.charge.detectorHp * 0.5));
    charge->setParameter(FCh::kParamStereoMode, 0.0f);
    charge->setParameter(FCh::kParamSidechain, 0.0f);
    charge->setParameter(FCh::kParamScGain, 0.5f);
    charge->setParameter(FCh::kParamMix, (float)(s.charge.mixPct / 100.0));
    charge->setParameter(FCh::kParamOutput, 0.5f);

    toneActive[0] = s.eq.lowShelfDb < -1e-6;
    toneActive[1] = std::fabs(s.eq.presenceDb) > 1e-6;
    toneActive[2] = s.eq.highShelfDb > 1e-6;
    if (toneActive[0]) loShelf.setLowShelf(100.0, 0.707, s.eq.lowShelfDb, sr);
    if (toneActive[1]) presence.setBell(3000.0, s.eq.presenceQ, s.eq.presenceDb, sr);
    if (toneActive[2]) hiShelf.setHighShelf(7000.0, 0.707, s.eq.highShelfDb, sr);

    // loudness push lives at the clip input (gain before the final stage);
    // set by Learn phase 2, clamped to the knob
    const double push = std::clamp(pushDb, -24.0, 24.0);
    clip->setParameter(FCl::kParamInput,   (float)(push / 48.0 + 0.5));
    clip->setParameter(FCl::kParamCeiling, (float)((s.clip.ceilingDb + 24.0) / 24.0));
    clip->setParameter(FCl::kParamKnee,    (float)(s.clip.kneePct / 100.0));
    clip->setParameter(FCl::kParamTilt,    (float)(s.clip.tiltPct / 100.0));
    clip->setParameter(FCl::kParamBias,    (float)(s.clip.biasPct / 200.0 + 0.5));
    clip->setParameter(FCl::kParamMix, 1.0f);
    clip->setParameter(FCl::kParamOutput, 0.5f);
    clip->setParameter(FCl::kParamAutoGain, 0.0f);
    clip->setParameter(FCl::kParamDelta, 0.0f);

    using FLm::FermentLimit;
    limit->setParameter(FLm::kParamGain, 0.0f);
    limit->setParameter(FLm::kParamCeiling, (float)((s.limit.ceilingDbtp + 24.0) / 24.0));
    limit->setParameter(FLm::kParamAttack,
        (float)(std::log(std::max(10.0, s.limit.attackMs) / 10.0) / 3.4012));
    limit->setParameter(FLm::kParamRelease,
        (float)(std::log(std::max(0.25, s.limit.releaseScale) / 0.25) / 2.7726));
    limit->setParameter(FLm::kParamTransLink, 0.7f);
    limit->setParameter(FLm::kParamTruePeak, s.limit.truePeak ? 1.0f : 0.0f);
    limit->setParameter(FLm::kParamDelta, 0.0f);
    limit->setParameter(FLm::kParamOutput, 0.5f);
}

void MasterCore::setPushDb(double db)
{
    pushDb = std::clamp(db, -24.0, 24.0);
    applyToEngines();
}

void MasterCore::completePhase2()
{
    if (learnPhase != 2) return;
    const auto r2 = learn.readout();

    /*  Same guard as learnFinish: if the 4 s window was silence (transport
        stopped right after phase 1), the short-term fallback is the gate
        floor and the push comes out pinned at +24.  Better no push than a
        blast; the colour from phase 1 stays, and a re-Learn sets the level. */
    if (!r2.loudnessReady && r2.lufsShortTerm < -70.0) {
        learnPhase = 0;
        return;
    }

    const double measured = r2.loudnessReady ? r2.lufsIntegrated
                                             : r2.lufsShortTerm;
    pushDb = std::clamp(current.targetLufs - measured, -24.0, 24.0);
    learnPhase = 0;
    applyToEngines();
}

void MasterCore::setBypass(Module m, bool bypassedFlag)
{
    auto& b = byp[m];
    b.target = bypassedFlag ? 0.0 : 1.0;
    const double fadeSamples = 0.010 * sr;
    b.step = (b.target - b.gain) / std::max(1.0, fadeSamples);
}

void MasterCore::runBypassMix(Bypass& b, const double* inDryL, const double* inDryR,
                              double* wetL, double* wetR, int n)
{
    for (int i = 0; i < n; ++i) {
        double dl = inDryL[i], dr = inDryR[i];
        if (b.delay > 0) {
            const double odl = b.dl[(size_t)b.idx];
            const double odr = b.dr[(size_t)b.idx];
            b.dl[(size_t)b.idx] = dl;
            b.dr[(size_t)b.idx] = dr;
            b.idx = (b.idx + 1) % b.delay;
            dl = odl; dr = odr;
        }
        if (b.step != 0.0) {
            b.gain += b.step;
            if ((b.step > 0 && b.gain >= b.target) ||
                (b.step < 0 && b.gain <= b.target)) { b.gain = b.target; b.step = 0.0; }
        }
        wetL[i] = dl + b.gain * (wetL[i] - dl);
        wetR[i] = dr + b.gain * (wetR[i] - dr);
    }
}

// --- audio ------------------------------------------------------------------

void MasterCore::process(double* L, double* R, int numSamples)
{
    // Without this, a call before prepare() spins forever rather than
    // failing: cap is 0, so n is 0 and pos never advances.
    if (cap <= 0 || numSamples <= 0)
        return;

    int pos = 0;
    while (pos < numSamples) {
        const int n = std::min(cap, numSamples - pos);
        double* l = L + pos;
        double* r = R + pos;

        pre.process(l, r, n);
        if (learnPhase == 1) learn.process(l, r, n);

        // --- Stage ------------------------------------------------------
        std::copy(l, l + n, dryL.begin());
        std::copy(r, r + n, dryR.begin());
        for (int i = 0; i < n; ++i) {
            double x = hp2.processL(hp1.processL(l[i])) * stageGainLin;
            double y = hp2.processR(hp1.processR(r[i])) * stageGainLin;
            if (widthFactor < 1.0) {
                const double mid = 0.5 * (x + y);
                const double side = 0.5 * (x - y) * widthFactor;
                x = mid + side; y = mid - side;
            }
            l[i] = x; r[i] = y;
        }
        runBypassMix(byp[ModStage], dryL.data(), dryR.data(), l, r, n);

        // --- Charge -------------------------------------------------------
        std::copy(l, l + n, dryL.begin());
        std::copy(r, r + n, dryR.begin());
        {
            double* ins[4]  = { l, r, silentA.data(), silentB.data() };
            double* outs[2] = { tL.data(), tR.data() };
            charge->processDoubleReplacing(ins, outs, n);
            std::copy(tL.begin(), tL.begin() + n, l);
            std::copy(tR.begin(), tR.begin() + n, r);
        }
        runBypassMix(byp[ModCharge], dryL.data(), dryR.data(), l, r, n);

        // --- Tone ---------------------------------------------------------
        std::copy(l, l + n, dryL.begin());
        std::copy(r, r + n, dryR.begin());
        for (int i = 0; i < n; ++i) {
            double x = l[i], y = r[i];
            if (toneActive[0]) { x = loShelf.processL(x);  y = loShelf.processR(y); }
            if (toneActive[1]) { x = presence.processL(x); y = presence.processR(y); }
            if (toneActive[2]) { x = hiShelf.processL(x);  y = hiShelf.processR(y); }
            l[i] = x; r[i] = y;
        }
        runBypassMix(byp[ModTone], dryL.data(), dryR.data(), l, r, n);

        // Learn phase 2: measure the coloured signal, then set the push.
        // Only audible chunks count.  After a transport stop the chain's
        // decay tail rings above the gate for a moment and then the input is
        // silence; measuring that window sets the push from the tail
        // (target minus -40 = the +24 blast).  Frozen instead: phase 2
        // simply waits until playback resumes and completes on real audio.
        if (learnPhase == 2) {
            double pw = 0.0;
            for (int i = 0; i < n; ++i) pw += l[i] * l[i] + r[i] * r[i];
            if (pw > 1e-7 * 2.0 * (double)n) {   // mean square over -70 dB
                learn.process(l, r, n);
                phase2Samples += n;
                if (phase2Samples >= (long long)(phase2AutoSec * sr))
                    completePhase2();
            }
        }

        // --- Clip ---------------------------------------------------------
        std::copy(l, l + n, dryL.begin());
        std::copy(r, r + n, dryR.begin());
        {
            double* ins[2]  = { l, r };
            double* outs[2] = { tL.data(), tR.data() };
            clip->processDoubleReplacing(ins, outs, n);
            std::copy(tL.begin(), tL.begin() + n, l);
            std::copy(tR.begin(), tR.begin() + n, r);
        }
        runBypassMix(byp[ModClip], dryL.data(), dryR.data(), l, r, n);

        // --- Limit --------------------------------------------------------
        std::copy(l, l + n, dryL.begin());
        std::copy(r, r + n, dryR.begin());
        {
            double* ins[2]  = { l, r };
            double* outs[2] = { tL.data(), tR.data() };
            limit->processDoubleReplacing(ins, outs, n);
            std::copy(tL.begin(), tL.begin() + n, l);
            std::copy(tR.begin(), tR.begin() + n, r);
        }
        runBypassMix(byp[ModLimit], dryL.data(), dryR.data(), l, r, n);

        post.process(l, r, n);
        pos += n;
    }
}

double MasterCore::chargeGrDb() const { return charge->meterGrDb(); }
double MasterCore::clipShaveDb() const { return clip->meterShaveDb(); }
double MasterCore::limitGrDb() const  { return limit->meterGrDb(); }

int MasterCore::latencySamples() const
{
    return FCl::kLatencySamples + FLm::FermentLimit::latencySamples(sr);
}

} // namespace ferment::master

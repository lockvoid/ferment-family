/* ========================================
 *  FermentLimit - FermentLimitProc.cpp
 *
 *  The DSP loop. Ported from the validated numpy prototype in
 *  the numpy reference; design rationale in the design notes.
 *
 *  Things that look odd but are load-bearing:
 *
 *   1. The sliding-min window is TRAILING and the dry path is delayed by
 *      exactly (window - 1) + TP group delay. Every smoother tap's window
 *      then contains the sample being scaled, which is the entire
 *      no-overshoot guarantee. An alignment slip of a few samples does not
 *      fail loudly — it quietly hands the limiting to the hygiene clamp
 *      (found the hard way in the prototype: a sliding-min origin sign
 *      flip cost 1.9 dB of true peak).
 *   2. The true-peak estimate is aligned by delaying the sample-magnitude
 *      detector path by the FIR group delay, not by shifting the estimate.
 *   3. total = max(transient, sustain) — not a sum, not a crossfade. The
 *      transient bound releases instantly by construction; the sustain
 *      floor is what stops re-pumping.
 * ======================================== */

#include "FermentLimit.h"

#include <algorithm>
#include <cmath>

namespace airwinconsolidated::FermentLimit {

static constexpr double kLn10Over20 = 0.11512925464970229;

static constexpr int    kControlBlock = 32;
static constexpr double kSmoothSeconds = 0.015;
static constexpr double kParamEpsilon = 1e-6;
static constexpr double kBoxRatio = 0.582;      // Signalsmith unequal pair
static constexpr double kCrestTauS = 0.2;
static constexpr double kReleaseMaxS = 0.6;
static constexpr double kReleaseClampLoS = 0.04;
static constexpr double kReleaseClampHiS = 1.2;

static constexpr bool kSmoothed[kNumParameters] = {
    true,   // kParamGain
    true,   // kParamCeiling
    false,  // kParamAttack   (coefficient only — output stays continuous)
    false,  // kParamRelease  (coefficient only)
    true,   // kParamTransLink
    false,  // kParamTruePeak (switch)
    false,  // kParamDelta    (switch)
    true,   // kParamOutput
};

static inline double dbToLin(double db) noexcept
{
    return std::exp(db * kLn10Over20);
}

static inline double glide(double current, double target, double coeff) noexcept
{
    const double v = target + (current - target) * coeff;
    return (std::fabs(v - target) < kParamEpsilon) ? target : v;
}

// --- true-peak interpolator design ------------------------------------------

void FermentLimit::designTp()
{
    const int n = kTpTapsPerPhase * 4;
    const double center = (n - 1) / 2.0;
    double full[kTpTapsPerPhase * 4];
    for (int i = 0; i < n; ++i) {
        const double t = (i - center) / 4.0;
        const double sinc = (std::fabs(t) < 1e-12)
                                ? 1.0
                                : std::sin(M_PI * t) / (M_PI * t);
        const double hann = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1));
        full[i] = sinc * hann;
    }
    for (int ph = 0; ph < 4; ++ph) {
        double s = 0.0;
        for (int t = 0; t < kTpTapsPerPhase; ++t) s += full[ph + 4 * t];
        for (int t = 0; t < kTpTapsPerPhase; ++t)
            tpPhase[ph][t] = full[ph + 4 * t] / s;
    }
}

// --- reset ------------------------------------------------------------------

void FermentLimit::reset() noexcept
{
    // Read the member, not getSampleRate(): the constructor calls reset()
    // before any host has set a rate, and the accessor asserts on that in
    // Debug builds. The < 8000 fallback covers the unset case either way.
    double sr = (double)sampleRate;
    if (sr < 8000.0) sr = 48000.0;
    cachedSr = sr;

    win = std::max(4, (int)std::lround(kAttackWindowMs * 1e-3 * sr));
    latency = latencySamples(sr);
    holdN = (int)(kHoldMs * 1e-3 * sr);

    const int n1 = std::max(1, (int)std::lround(win * kBoxRatio));
    const int n2 = std::max(1, win - n1);
    for (Lane* lane : { &laneL, &laneR }) {
        lane->smin.init(win);
        lane->box1.init(n1);
        lane->box2.init(n2);
        lane->xRing.assign((size_t)kTpTapsPerPhase, 0.0);
        lane->magRing.assign((size_t)(kTpTapsPerPhase / 2), 0.0);
        lane->xIdx = lane->magIdx = 0;
    }
    dryL.assign((size_t)latency, 0.0);
    dryR.assign((size_t)latency, 0.0);
    dryIdx = 0;

    designTp();

    pk2 = rms2 = 0.0;
    susDb = 0.0;
    holdCount = 0;
    grMeterDb = 0.0;
    smoothPrimed = false;
}

// --- the loop ---------------------------------------------------------------

template <typename T>
void FermentLimit::processT(T** inputs, T** outputs, VstInt32 sampleFrames)
{
    T* in1 = inputs[0];
    T* in2 = inputs[1];
    T* out1 = outputs[0];
    T* out2 = outputs[1];

    double sr = getSampleRate();
    if (sr < 8000.0) sr = 48000.0;
    if (sr != cachedSr) reset();

    if (!smoothPrimed) {
        for (int i = 0; i < kNumParameters; ++i) sm[i] = p[i];
        smoothPrimed = true;
    }
    const double smCoeff = std::exp(-(double)kControlBlock / (kSmoothSeconds * sr));
    const double aCrest = std::exp(-1.0 / (kCrestTauS * sr));

    double meterPk = 0.0;

    int pos = 0;
    while (pos < sampleFrames) {
        const int n = std::min((VstInt32)kControlBlock, sampleFrames - pos);

        for (int i = 0; i < kNumParameters; ++i)
            sm[i] = kSmoothed[i] ? glide(sm[i], (double)p[i], smCoeff) : (double)p[i];

        const double inG      = dbToLin(gainDbFromNorm(sm[kParamGain]));
        const double outG     = dbToLin(trimDbFromNorm(sm[kParamOutput]));
        const double ceilDb   = ceilingDbFromNorm(sm[kParamCeiling]);
        const bool   tpOn     = p[kParamTruePeak] >= 0.5f;
        const bool   deltaOn  = p[kParamDelta] >= 0.5f;
        const double link     = sm[kParamTransLink];
        const double ceilHard = dbToLin(ceilDb);
        const double ceilEff  = dbToLin(ceilDb - (tpOn ? kTpMarginDb : 0.0));

        const double aAtt = std::exp(-1.0 / (attackMsFromNorm(sm[kParamAttack])
                                             * 1e-3 * sr));
        // Auto-release from the current crest factor, once per control block.
        const double crest2 = std::max(pk2 / std::max(rms2, 1e-18), 1.0);
        double tauR = 2.0 * kReleaseMaxS / crest2
                      * releaseScaleFromNorm(sm[kParamRelease]);
        tauR = std::min(std::max(tauR, kReleaseClampLoS), kReleaseClampHiS);
        const double aRel = std::exp(-1.0 / (tauR * sr));

        for (int s = 0; s < n; ++s, ++pos) {
            const double L = (double)in1[pos] * inG;
            const double R = (double)in2[pos] * inG;

            // detector front end per lane: TP estimate now, |x| delayed by
            // the estimator's group delay so both describe the same instant
            auto front = [&](Lane& lane, double x, double& magDel,
                             double& tpEst) {
                lane.xRing[(size_t)lane.xIdx] = x;
                tpEst = 0.0;
                if (tpOn) {
                    for (int ph = 0; ph < 4; ++ph) {
                        double acc = 0.0;
                        int idx = lane.xIdx;
                        for (int t = 0; t < kTpTapsPerPhase; ++t) {
                            acc += tpPhase[ph][t] * lane.xRing[(size_t)idx];
                            idx = (idx + kTpTapsPerPhase - 1) % kTpTapsPerPhase;
                        }
                        tpEst = std::max(tpEst, std::fabs(acc));
                    }
                }
                lane.xIdx = (lane.xIdx + 1) % kTpTapsPerPhase;

                const int magLen = kTpTapsPerPhase / 2;
                magDel = lane.magRing[(size_t)lane.magIdx];
                lane.magRing[(size_t)lane.magIdx] = std::fabs(x);
                lane.magIdx = (lane.magIdx + 1) % magLen;
            };

            double magL, tpL, magR, tpR;
            front(laneL, L, magL, tpL);
            front(laneR, R, magR, tpR);

            const double detL = std::max({ magL, link * magR, tpL });
            const double detR = std::max({ magR, link * magL, tpR });

            const double d = std::max(detL, detR);
            const double d2 = d * d;
            pk2 = std::max(d2, aCrest * pk2 + (1.0 - aCrest) * d2);
            rms2 = aCrest * rms2 + (1.0 - aCrest) * d2;

            auto transient = [&](Lane& lane, double det) -> double {
                const double need = std::min(1.0, ceilEff / std::max(det, 1e-12));
                double g = lane.box2.process(lane.box1.process(
                    lane.smin.process(need)));
                if (g > 1.0) g = 1.0;
                return -20.0 * std::log10(std::max(g, 1e-12));
            };
            const double grL = transient(laneL, detL);
            const double grR = transient(laneR, detR);

            const double grLink = std::max(grL, grR);
            if (grLink > susDb) {
                susDb = grLink + (susDb - grLink) * aAtt;
                holdCount = holdN;
            } else if (holdCount > 0) {
                --holdCount;
            } else {
                susDb = grLink + (susDb - grLink) * aRel;
            }

            const double totL = std::max(grL, susDb);
            const double totR = std::max(grR, susDb);
            if (totL > meterPk) meterPk = totL;
            if (totR > meterPk) meterPk = totR;

            const double dL = dryL[(size_t)dryIdx];
            const double dR = dryR[(size_t)dryIdx];
            dryL[(size_t)dryIdx] = L;
            dryR[(size_t)dryIdx] = R;
            dryIdx = (dryIdx + 1) % latency;

            double wetL = dL * std::exp(-totL * kLn10Over20);
            double wetR = dR * std::exp(-totR * kLn10Over20);
            wetL = std::max(-ceilHard, std::min(ceilHard, wetL));
            wetR = std::max(-ceilHard, std::min(ceilHard, wetR));

            double oL, oR;
            if (deltaOn) {
                oL = (dL - wetL) * outG;
                oR = (dR - wetR) * outG;
            } else {
                oL = wetL * outG;
                oR = wetR * outG;
            }

            out1[pos] = (T)oL;
            out2[pos] = (T)oR;
        }
    }

    grMeterDb = meterPk;
}

void FermentLimit::processReplacing(float** inputs, float** outputs, VstInt32 sampleFrames)
{
    processT(inputs, outputs, sampleFrames);
}

void FermentLimit::processDoubleReplacing(double** inputs, double** outputs, VstInt32 sampleFrames)
{
    processT(inputs, outputs, sampleFrames);
}

} // namespace airwinconsolidated::FermentLimit

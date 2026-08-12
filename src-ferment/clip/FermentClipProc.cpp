/* ========================================
 *  FermentClip - FermentClipProc.cpp
 *
 *  The DSP loop. Ported from the measured and validated numpy reference
 *  implementation.
 *
 *  Three deliberate choices, do not "correct" without the design doc:
 *
 *   1. Oversampling is polyphase allpass IIR (minimum-phase-ish), not
 *      linear-phase FIR: pre-ring would smear clipped kick attacks backward
 *      and overshoot the ceiling after decimation.
 *   2. The de-emphasis stage inverts the *actual* pre-emphasis biquads by
 *      swapping numerator/denominator. An RBJ shelf with negated gain is not
 *      the inverse (the damping term differs) and leaves a residual tilt.
 *   3. The clipper is memoryless by design (ADAA aside): no thresholds with
 *      ballistics, no adaptive behaviour. Its whole advantage over a limiter
 *      is the absence of time constants.
 * ======================================== */

#include "FermentClip.h"

#include <algorithm>
#include <cmath>

namespace airwinconsolidated::FermentClip {

static constexpr double kLn10Over20 = 0.11512925464970229;

// Vicanek droop compensation: 1st-order ADAA is a 2-tap average in its
// linear regime (-0.5 dB at 20 kHz seen from the 4x rate). A one-pole
// b/(1 + a z^-1) with a = 1/(3+sqrt(8)) before and after the shaper inverts
// that response to within thousandths of a dB across the audio band.
static constexpr double kAdaaCompA = 0.17157287525380990;
static constexpr double kAdaaCompB = 1.0 + kAdaaCompA;

// --- halfband coefficients --------------------------------------------------
// Elliptic-derived allpass set (ascending), split even/odd into the two
// polyphase paths. Measured through the actual two-path structure:
// passband ripple 0.000 dB (<= 0.227 fs), stopband -104.5 dB (>= 0.273 fs),
// group delay ~4.04 samples at the stage rate. Both cascade stages use the
// same set — steeper than the second stage strictly needs, but the cost is
// six first-order allpasses.
static constexpr double kHbEven[6] = {
    0.036681502163648017, 0.27463175937945444, 0.56109869787919531,
    0.76974183386322703,  0.89226081800387902, 0.96209454837808417
};
static constexpr double kHbOdd[6] = {
    0.13654762463195794, 0.42313861743656711, 0.67754004997416184,
    0.83988962484963892, 0.9315419599631839,  0.98781637073289585
};

// --- parameter smoothing (charge-style control blocks) -----------------------
static constexpr int    kControlBlock = 32;
static constexpr double kSmoothSeconds = 0.015;
static constexpr double kParamEpsilon = 1e-6;

static constexpr bool kSmoothed[kNumParameters] = {
    true,   // kParamInput
    true,   // kParamCeiling
    true,   // kParamKnee
    true,   // kParamTilt
    true,   // kParamBias
    true,   // kParamMix
    true,   // kParamOutput
    false,  // kParamAutoGain (switch)
    false,  // kParamDelta    (switch)
};

// --- helpers ----------------------------------------------------------------

static inline double dbToLin(double db) noexcept
{
    return std::exp(db * kLn10Over20);
}

static inline double glide(double current, double target, double coeff) noexcept
{
    const double v = target + (current - target) * coeff;
    return (std::fabs(v - target) < kParamEpsilon) ? target : v;
}

/* Piecewise-quadratic morphing knee, ceiling-normalised (1.0 = clip point).
 * Linear below 1-k, C1 at both joints, flat above 1+k. k in (0, 1]. */
static inline double kneeCurve(double x, double k) noexcept
{
    const double ax = std::fabs(x);
    double y;
    if (ax <= 1.0 - k)      y = ax;
    else if (ax < 1.0 + k) {
        const double t = ax - (1.0 - k);
        y = ax - t * t / (4.0 * k);
    }
    else                    y = 1.0;
    if (y > 1.0) y = 1.0;
    return x < 0.0 ? -y : y;
}

/* First antiderivative of kneeCurve (even function). */
static inline double kneeAntideriv(double x, double k) noexcept
{
    const double ax = std::fabs(x);
    if (ax <= 1.0 - k) return 0.5 * ax * ax;
    if (ax < 1.0 + k) {
        const double t = ax - (1.0 - k);
        return 0.5 * ax * ax - t * t * t / (12.0 * k);
    }
    const double cHi = 0.5 * (1.0 + k) * (1.0 + k) - (2.0 / 3.0) * k * k - (1.0 + k);
    return ax + cHi;
}

/* Bias is implemented as asymmetric ceilings — one polarity clips earlier —
 * NOT as a DC shift into the shaper. A DC-shifted wave pierces the ceiling
 * on the far side, and the safety clamp then rectifies it back into DC.
 * cp/cm scale the positive/negative clip points (<= 1). */
static inline double kneeAsym(double x, double k, double cp, double cm) noexcept
{
    return x >= 0.0 ? cp * kneeCurve(x / cp, k)
                    : cm * kneeCurve(x / cm, k);
}

static inline double kneeAsymAntideriv(double x, double k, double cp, double cm) noexcept
{
    // Antiderivative of kneeAsym; kneeAntideriv is even with F(0) = 0, so the
    // two branches join continuously at zero.
    return x >= 0.0 ? cp * cp * kneeAntideriv(x / cp, k)
                    : cm * cm * kneeAntideriv(x / cm, k);
}

/* 1st-order ADAA: y = (F1(x) - F1(xPrev)) / (x - xPrev), midpoint fallback
 * where ill-conditioned. Half-sample delay at the oversampled rate. */
static inline double adaaKnee(double x, double& xPrev, double k,
                              double cp, double cm) noexcept
{
    const double d = x - xPrev;
    double y;
    if (std::fabs(d) < 1e-6)
        y = kneeAsym(0.5 * (x + xPrev), k, cp, cm);
    else
        y = (kneeAsymAntideriv(x, k, cp, cm)
             - kneeAsymAntideriv(xPrev, k, cp, cm)) / d;
    xPrev = x;
    return y;
}

// --- tilt -------------------------------------------------------------------

void FermentClip::updateTilt(double srOs, double tiltAmt)
{
    if (srOs == cachedSrOs && std::fabs(tiltAmt - cachedTilt) < 1e-9) return;
    cachedSrOs = srOs;
    cachedTilt = tiltAmt;

    const double g = 4.5 * tiltAmt;   // dB at the shelf extremes
    tiltHi.setHighShelf(1400.0, 0.45, +g, srOs);
    tiltLo.setLowShelf(280.0, 0.45, -g, srOs);
    // Exact inverse of a minimum-phase biquad: swap numerator/denominator.
    tiltHiInv.setFromCookbook(1.0, tiltHi.a1, tiltHi.a2,
                              tiltHi.b0, tiltHi.b1, tiltHi.b2);
    tiltLoInv.setFromCookbook(1.0, tiltLo.a1, tiltLo.a2,
                              tiltLo.b0, tiltLo.b1, tiltLo.b2);
}

// --- reset ------------------------------------------------------------------

void FermentClip::reset() noexcept
{
    auto wire = [](OversamplerChain& os) {
        HalfbandPath* even[4] = { &os.upA.p0, &os.upB.p0, &os.downB.p0, &os.downA.p0 };
        HalfbandPath* odd[4]  = { &os.upA.p1, &os.upB.p1, &os.downB.p1, &os.downA.p1 };
        for (int i = 0; i < 4; ++i) {
            even[i]->c = kHbEven; even[i]->n = 6;
            odd[i]->c  = kHbOdd;  odd[i]->n  = 6;
        }
        os.reset();
    };
    wire(osL);
    wire(osR);

    tiltHi.reset(); tiltLo.reset(); tiltHiInv.reset(); tiltLoInv.reset();
    cachedSrOs = 0.0;
    cachedTilt = -1.0;

    adaaPrevL = adaaPrevR = 0.0;
    compPreL = compPostL = compPreR = compPostR = 0.0;
    dcXL = dcYL = dcXR = dcYR = 0.0;
    msIn = msWet = 0.0;
    autoG = 1.0;
    for (int i = 0; i <= kDryMask; ++i) dryL[i] = dryR[i] = 0.0;
    dryIdx = 0;
    shaveDb = 0.0;
    smoothPrimed = false;
}

// --- the loop ---------------------------------------------------------------

template <typename T>
void FermentClip::processT(T** inputs, T** outputs, VstInt32 sampleFrames)
{
    T* in1 = inputs[0];
    T* in2 = inputs[1];
    T* out1 = outputs[0];
    T* out2 = outputs[1];

    // The member, not getSampleRate(): the accessor asserts on a degenerate
    // rate in Debug builds, before the fallback below ever gets to run.
    double sr = (double)sampleRate;
    if (sr < 8000.0) sr = 48000.0;
    const double srOs = sr * 4.0;

    if (!smoothPrimed) {
        for (int i = 0; i < kNumParameters; ++i) sm[i] = p[i];
        smoothPrimed = true;
    }
    const double smCoeff = std::exp(-(double)kControlBlock / (kSmoothSeconds * sr));
    const double msCoeff = std::exp(-1.0 / (1.0 * sr));     // RMS trackers, 1 s
    const double agCoeff = std::exp(-1.0 / (0.25 * sr));    // auto-gain glide

    double shavePk = 1.0;

    int pos = 0;
    while (pos < sampleFrames) {
        const int n = std::min((VstInt32)kControlBlock, sampleFrames - pos);

        for (int i = 0; i < kNumParameters; ++i)
            sm[i] = kSmoothed[i] ? glide(sm[i], (double)p[i], smCoeff) : (double)p[i];

        const double inG    = dbToLin(trimDbFromNorm(sm[kParamInput]));
        const double outG   = dbToLin(trimDbFromNorm(sm[kParamOutput]));
        const double ceil   = dbToLin(ceilingDbFromNorm(sm[kParamCeiling]));
        const double knee   = std::max(1e-3, std::min(1.0, sm[kParamKnee]));
        const double bias   = biasFromNorm(sm[kParamBias]);
        const double mix    = sm[kParamMix];
        const bool   autoOn  = p[kParamAutoGain] >= 0.5f;
        const bool   deltaOn = p[kParamDelta] >= 0.5f;
        const double cPos   = 1.0 - 0.3 * std::max(0.0,  bias);
        const double cNeg   = 1.0 - 0.3 * std::max(0.0, -bias);
        const double invCeil = 1.0 / ceil;

        updateTilt(srOs, sm[kParamTilt]);

        // One oversampled nonlinear lane. isR selects the biquad channel
        // state; everything else is passed per channel.
        auto lane = [&](double x, OversamplerChain& os, double& adaaPrev,
                        double& compPre, double& compPost, bool isR) -> double {
            double a0, a1;
            os.upA.process(x, a0, a1);
            double v[4];
            os.upB.process(a0, v[0], v[1]);
            os.upB.process(a1, v[2], v[3]);
            for (int j = 0; j < 4; ++j) {
                double u = isR ? tiltLo.processR(tiltHi.processR(v[j]))
                               : tiltLo.processL(tiltHi.processL(v[j]));
                u = kAdaaCompB * u - kAdaaCompA * compPre;
                compPre = u;
                u = u * invCeil;
                const double over = std::fabs(u);
                if (over > shavePk) shavePk = over;
                double y = adaaKnee(u, adaaPrev, knee, cPos, cNeg);
                y *= ceil;
                y = kAdaaCompB * y - kAdaaCompA * compPost;
                compPost = y;
                v[j] = isR ? tiltHiInv.processR(tiltLoInv.processR(y))
                           : tiltHiInv.processL(tiltLoInv.processL(y));
            }
            const double d0 = os.downB.process(v[0], v[1]);
            const double d1 = os.downB.process(v[2], v[3]);
            return os.downA.process(d0, d1);
        };

        for (int s = 0; s < n; ++s, ++pos) {
            const double L = (double)in1[pos] * inG;
            const double R = (double)in2[pos] * inG;

            dryL[dryIdx] = L;
            dryR[dryIdx] = R;
            const int rd = (dryIdx - kLatencySamples) & kDryMask;
            const double dL = dryL[rd], dR = dryR[rd];
            dryIdx = (dryIdx + 1) & kDryMask;

            msIn = msIn * msCoeff + (1.0 - msCoeff) * 0.5 * (L * L + R * R);

            double wetL = lane(L, osL, adaaPrevL, compPreL, compPostL, false);
            double wetR = lane(R, osR, adaaPrevR, compPreR, compPostR, true);

            // Asymmetric clipping accumulates offset; block it, but only
            // while bias is engaged so the symmetric path stays untouched.
            if (bias != 0.0) {
                const double kDc = 1.0 - 2.0 * M_PI * 5.0 / sr;
                double y = wetL - dcXL + kDc * dcYL; dcXL = wetL; dcYL = y; wetL = y;
                y = wetR - dcXR + kDc * dcYR; dcXR = wetR; dcYR = y; wetR = y;
            } else {
                dcXL = dcYL = dcXR = dcYR = 0.0;
            }

            msWet = msWet * msCoeff + (1.0 - msCoeff) * 0.5 * (wetL * wetL + wetR * wetR);

            // RMS-matched makeup (not naive inverse drive): defeats the
            // louder-is-better bias when judging drive amounts.
            double agTarget = 1.0;
            if (autoOn) {
                agTarget = std::sqrt((msIn + 1e-12) / (msWet + 1e-12));
                agTarget = std::max(0.25, std::min(4.0, agTarget));
            }
            autoG = agTarget + (autoG - agTarget) * agCoeff;
            wetL *= autoG;
            wetR *= autoG;

            double oL, oR;
            if (deltaOn) {
                oL = (dL - wetL) * outG;
                oR = (dR - wetR) * outG;
            } else {
                oL = (mix * wetL + (1.0 - mix) * dL) * outG;
                oR = (mix * wetR + (1.0 - mix) * dR) * outG;
                // Safety: decimation ripple can poke fractions of a dB over.
                oL = std::max(-ceil, std::min(ceil, oL));
                oR = std::max(-ceil, std::min(ceil, oR));
            }

            out1[pos] = (T)oL;
            out2[pos] = (T)oR;
        }
    }

    shaveDb = 20.0 * std::log10(shavePk);
}

void FermentClip::processReplacing(float** inputs, float** outputs, VstInt32 sampleFrames)
{
    processT(inputs, outputs, sampleFrames);
}

void FermentClip::processDoubleReplacing(double** inputs, double** outputs, VstInt32 sampleFrames)
{
    processT(inputs, outputs, sampleFrames);
}

} // namespace airwinconsolidated::FermentClip

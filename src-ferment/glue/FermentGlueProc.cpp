/* ========================================
 *  FermentGlue - FermentGlueProc.cpp
 *
 *  Feed-forward bus compressor. Implementation references:
 *    Giannoulis, Massberg, Reiss, "Digital Dynamic Range Compressor
 *    Design — A Tutorial and Analysis", JAES 60(6), 2012 — for the
 *    soft-knee formula and one-pole coefficient α = exp(-1/(τ·fs)).
 *    SSL 4000 G bus compressor public hardware notes — for the
 *    two-time-constant program-dependent Auto release (fast cap ~30 ms,
 *    slow cap ~400 ms, combined so fast transients recover quickly while
 *    sustained loud material is held down).
 *
 *  This is a hand-rolled SSL-inspired compressor, not a sonic clone of
 *  Cytomic's or any other commercial SSL G emulation.
 * ======================================== */

#ifndef __FermentGlue_H
#include "FermentGlue.h"
#endif
#include <cmath>
#include <cstdlib>
#include <algorithm>
namespace airwinconsolidated::FermentGlue {

// --- Coefficient helpers ---
static inline double tauToCoeff(double tauSeconds, double sr)
{
    if (tauSeconds <= 0.0) return 0.0;  // instantaneous
    return std::exp(-1.0 / (tauSeconds * sr));
}

// Soft-knee gain computer (Giannoulis/Massberg/Reiss, eq. 4):
// levelDb in, returns gain reduction in dB (>=0).
static inline double softKneeGR(double levelDb, double thresholdDb, double ratio, double kneeDb)
{
    const double diff = levelDb - thresholdDb;
    if (2.0 * diff < -kneeDb)
        return 0.0;  // below knee → no GR

    double yG;
    if (std::abs(2.0 * diff) <= kneeDb)
    {
        // knee region
        const double x = diff + kneeDb * 0.5;
        yG = levelDb + (1.0 / ratio - 1.0) * x * x / (2.0 * kneeDb);
    }
    else
    {
        // above knee
        yG = thresholdDb + diff / ratio;
    }

    double gr = levelDb - yG;
    if (gr < 0.0) gr = 0.0;
    return gr;
}

// Smooth soft-clipper: tanh-like saturation that is unity-gain at low levels
// and asymptotes to ±1.  Using a rational approximation (faster than tanh,
// close enough for a musical clipper).
static inline double softClipOne(double x)
{
    // tanh-like: x / sqrt(1 + x^2) would hit ±1 eventually; we use a softer
    // curve that's closer to tanh and stays linear further out.
    // y = x / (1 + |x|)^0.5-ish... simplest stable: x / sqrt(1 + x*x)
    // but that hits ±1 too quickly.  Use this instead (normalised tanh approx):
    const double ax = std::fabs(x);
    const double k  = x / (1.0 + ax);           // smooth but only hits ±1 at infinity
    // Blend between linear (for |x|<0.5) and soft-clipped (for |x|>0.5)
    // so low-level signals are untouched.
    if (ax < 0.5) return x;
    return std::tanh(x);
}

template <typename T>
static void processInternal(FermentGlue &self, T **inputs, T **outputs, VstInt32 sampleFrames,
                            double sampleRate,
                            float A, float B, float C, float D, float E, float F, float G, float H,
                            float I, float J,
                            double &envRMS, double &envGR,
                            double &hpfL_z1, double &hpfR_z1,
                            uint32_t &fpdL, uint32_t &fpdR)
{
    T *in1  = inputs[0];
    T *in2  = inputs[1];
    T *sc1  = inputs[2];
    T *sc2  = inputs[3];
    T *out1 = outputs[0];
    T *out2 = outputs[1];

    // --- Derive coefficients once per block ---
    const double thresholdDb = ((double)A - 1.0) * 40.0;       // -40 .. 0 dB
    const double ratio       = (B < 0.33f) ? 2.0
                             : (B < 0.67f) ? 4.0 : 10.0;
    const double attackMs    = 0.03 * std::pow(1000.0, (double)C);   // 0.03 .. 30 ms
    const bool   autoRelease = (D >= 0.9f);
    const double userReleaseMs = autoRelease ? 300.0
                                             : 100.0 * std::pow(12.0, (double)D / 0.9);
    const double makeupDb    = (double)E * 24.0;
    const double makeupLin   = std::pow(10.0, makeupDb / 20.0);
    const double rangeDb     = (double)F * 60.0;
    const double wet         = (double)G;
    const double dry         = 1.0 - wet;
    const bool   scOn        = (H >= 0.5f);
    const bool   softClipOn  = (J >= 0.5f);
    const double kneeDb      = 6.0;  // 6 dB knee width — classic musical knee
    const double hpfFc       = 20.0 * std::pow(25.0, (double)I);  // 20 .. 500 Hz log scale

    const double attackCoeff      = tauToCoeff(attackMs * 1e-3, sampleRate);
    const double manualReleaseC   = tauToCoeff(userReleaseMs * 1e-3, sampleRate);

    // Auto: fast cap ~30 ms release, slow cap ~400 ms release with slow ~2 s attack
    // (matches SSL Auto: short transients recover fast, sustained events release slow)
    const double fastReleaseCoeff = tauToCoeff(0.030, sampleRate);
    const double slowReleaseCoeff = tauToCoeff(0.400, sampleRate);
    const double slowAttackCoeff  = tauToCoeff(2.000, sampleRate);

    // Detector HPF (one-pole, user-controllable 20 .. 500 Hz) applied to
    // detector signal to reduce sub-bass over-triggering — classic SSL-bus practice.
    const double hpfAlpha = std::exp(-2.0 * M_PI * hpfFc / sampleRate);  // one-pole HPF coeff

    // --- Use a second envelope state for Auto (slow branch) ---
    // envGR is the primary (fast) envelope.  For Auto we also track a
    // slow-charging / slow-releasing envelope via the RMS slot.
    double &envFast = envGR;
    double &envSlow = envRMS;  // repurpose as the slow envelope state

    while (--sampleFrames >= 0)
    {
        double inL = (double)*in1;
        double inR = (double)*in2;
        double scL = (double)*sc1;
        double scR = (double)*sc2;

        // denormal guards
        if (std::fabs(inL) < 1.18e-23) inL = fpdL * 1.18e-17;
        if (std::fabs(inR) < 1.18e-23) inR = fpdR * 1.18e-17;

        const double drySampleL = inL;
        const double drySampleR = inR;

        // --- Detector source ---
        double detL = scOn ? scL : inL;
        double detR = scOn ? scR : inR;

        // --- Detector HPF (remove sub-bass from triggering) ---
        // One-pole HPF: y[n] = alpha * (y[n-1] + x[n] - x[n-1])
        // We implement as: hp = x - z; z = z + (1-alpha)*hp
        {
            double hpL = detL - hpfL_z1;
            hpfL_z1 += (1.0 - hpfAlpha) * hpL;
            detL = hpL;

            double hpR = detR - hpfR_z1;
            hpfR_z1 += (1.0 - hpfAlpha) * hpR;
            detR = hpR;
        }

        // --- Level in dB (abs max of L/R channels) ---
        double level = std::fabs(detL);
        if (std::fabs(detR) > level) level = std::fabs(detR);
        const double levelDb = 20.0 * std::log10(level + 1e-30);

        // --- Soft-knee gain reduction target (dB) ---
        double targetGr = softKneeGR(levelDb, thresholdDb, ratio, kneeDb);

        // Range limit: smooth clamp to maximum allowed GR.
        if (targetGr > rangeDb) targetGr = rangeDb;

        // --- Envelope ballistics ---
        // Primary (fast) envelope: user's attack, user's release (or 30 ms in Auto).
        {
            const double releaseC = autoRelease ? fastReleaseCoeff : manualReleaseC;
            const double coeff = (targetGr > envFast) ? attackCoeff : releaseC;
            envFast = coeff * envFast + (1.0 - coeff) * targetGr;
        }

        // Slow envelope: only used in Auto mode.  Slow attack so it only
        // accumulates on sustained loud events; slow release holds the tail.
        if (autoRelease)
        {
            const double coeff = (targetGr > envSlow) ? slowAttackCoeff : slowReleaseCoeff;
            envSlow = coeff * envSlow + (1.0 - coeff) * targetGr;
        }
        else
        {
            // decay back to zero when Auto is off, so state is clean if user flips back
            envSlow *= slowReleaseCoeff;
        }

        const double grDb = autoRelease ? std::max(envFast, envSlow) : envFast;

        // --- Apply gain reduction + makeup to main signal ---
        const double gainLin = std::pow(10.0, -grDb / 20.0);
        double wetL = inL * gainLin * makeupLin;
        double wetR = inR * gainLin * makeupLin;

        // --- Dry/Wet blend ---
        double outL = dry * drySampleL + wet * wetL;
        double outR = dry * drySampleR + wet * wetR;

        // --- Soft-clip output ceiling ---
        if (softClipOn)
        {
            outL = softClipOne(outL);
            outR = softClipOne(outR);
        }

        // --- Dither ---
        if (std::is_same<T, float>::value)
        {
            int expon;
            std::frexp((float)outL, &expon);
            fpdL ^= fpdL << 13; fpdL ^= fpdL >> 17; fpdL ^= fpdL << 5;
            outL += ((double(fpdL) - (double)0x7fffffff) * 5.5e-36l * std::pow(2.0, expon + 62));
            std::frexp((float)outR, &expon);
            fpdR ^= fpdR << 13; fpdR ^= fpdR >> 17; fpdR ^= fpdR << 5;
            outR += ((double(fpdR) - (double)0x7fffffff) * 5.5e-36l * std::pow(2.0, expon + 62));
        }
        else
        {
            // 64-bit path: just advance dither state, don't inject (like other Airwindows)
            fpdL ^= fpdL << 13; fpdL ^= fpdL >> 17; fpdL ^= fpdL << 5;
            fpdR ^= fpdR << 13; fpdR ^= fpdR >> 17; fpdR ^= fpdR << 5;
        }

        *out1 = (T)outL;
        *out2 = (T)outR;

        in1++; in2++; sc1++; sc2++;
        out1++; out2++;
    }
}

void FermentGlue::processReplacing(float **inputs, float **outputs, VstInt32 sampleFrames)
{
    processInternal<float>(*this, inputs, outputs, sampleFrames, getSampleRate(),
                           A, B, C, D, E, F, G, H, I, J,
                           envRMS, envGR, hpfL_z1, hpfR_z1, fpdL, fpdR);
}

void FermentGlue::processDoubleReplacing(double **inputs, double **outputs, VstInt32 sampleFrames)
{
    processInternal<double>(*this, inputs, outputs, sampleFrames, getSampleRate(),
                            A, B, C, D, E, F, G, H, I, J,
                            envRMS, envGR, hpfL_z1, hpfR_z1, fpdL, fpdR);
}

} // end namespace

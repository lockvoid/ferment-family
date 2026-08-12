/* ========================================
 *  Ferment Analyzer — analyzer-core implementation
 *
 *  The unit tests pin this implementation to synthetic ground truth
 *  (tests/analyzer_test.cpp: EBU reference tones, known spectra, gated
 *  loudness invariants).
 * ======================================== */

#include "AnalyzerCore.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace ferment::analyzer {

static constexpr double kAbsGateLu = -70.0;
static constexpr double kRelGateLu = -10.0;

Profile profileStudio() { return {"studio", -9.5, -1.5, -15.0, -16.0}; }
Profile profileReel()   { return {"reel",   -8.5, -1.0, -13.0, -14.0}; }

// --- K-weighting design (ITU-R BS.1770 analog prototype, bilinear at fs) ---
// Shelf: fc 1681.9744509555319 Hz, gain +3.999843853973347 dB, Q 0.7071752369554196
// HP:    fc 38.13547087602444 Hz, Q 0.5003270373238773
void AnalyzerCore::KLane::design(double fs)
{
    {
        const double fc = 1681.9744509555319, G = 3.999843853973347,
                     Q = 0.7071752369554196;
        const double K = std::tan(M_PI * fc / fs);
        const double Vh = std::pow(10.0, G / 20.0);
        const double Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
        shelf.b1 = 2.0 * (K * K - Vh) / a0;
        shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0;
        shelf.a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double fc = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan(M_PI * fc / fs);
        const double a0 = 1.0 + K / Q + K * K;
        hp.b0 = 1.0;
        hp.b1 = -2.0;
        hp.b2 = 1.0;
        hp.a1 = 2.0 * (K * K - 1.0) / a0;
        hp.a2 = (1.0 - K / Q + K * K) / a0;
        const double norm = 1.0 / a0;
        hp.b0 *= norm; hp.b1 *= norm; hp.b2 *= norm;
    }
    reset();
}

// --- lifecycle --------------------------------------------------------------

void AnalyzerCore::prepare(double sampleRate)
{
    sr = sampleRate;
    hopSamples = (int)std::lround(0.1 * sr);
    kL.design(sr); kR.design(sr); kM.design(sr);

    // 4x true-peak interpolator, Hann-windowed sinc (same design the
    // limiter's detector uses).
    const int n = kTpTaps * 4;
    const double center = (n - 1) / 2.0;
    double full[kTpTaps * 4];
    for (int i = 0; i < n; ++i) {
        const double t = (i - center) / 4.0;
        const double sinc = std::fabs(t) < 1e-12 ? 1.0
                            : std::sin(M_PI * t) / (M_PI * t);
        full[i] = sinc * (0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1)));
    }
    for (int ph = 0; ph < 4; ++ph) {
        double s = 0.0;
        for (int t = 0; t < kTpTaps; ++t) s += full[ph + 4 * t];
        for (int t = 0; t < kTpTaps; ++t) tpPhase[ph][t] = full[ph + 4 * t] / s;
    }

    fftRing.assign(kFft, 0.0);
    fftScratch.assign((size_t)kFft, {});
    // One entry per 100 ms hop (the 400 ms blocks overlap 75 %), so an hour
    // of audio is 36 000 — my first comment here said 9000 was an hour and
    // was wrong by 4x, which showed up as reallocations mid-session.
    gateBlocks.reserve(36000);
    psdAvg.assign(kFft / 2 + 1, 0.0);
    psdSum.assign(kFft / 2 + 1, 0.0);
    hannWin.resize(kFft);
    for (int i = 0; i < kFft; ++i)
        hannWin[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (kFft - 1));

    stPowRing.assign(kStHops, 0.0);
    stMonoRing.assign(kStHops, 0.0);
    stPeakRing.assign(kStHops, 0.0);
    stRmsRing.assign(kStHops, 0.0);
    stTpRing.assign(kStHops, 0.0);
    reset();
}

void AnalyzerCore::reset()
{
    kL.reset(); kR.reset(); kM.reset();
    hopFill = 0;
    hopPowSt = hopPowMono = hopPeak = hopRawPow = hopTpPeak = 0.0;
    gateBlocks.clear();
    std::fill(stPowRing.begin(), stPowRing.end(), 0.0);
    std::fill(stMonoRing.begin(), stMonoRing.end(), 0.0);
    std::fill(stPeakRing.begin(), stPeakRing.end(), 0.0);
    std::fill(stRmsRing.begin(), stRmsRing.end(), 0.0);
    std::fill(stTpRing.begin(), stTpRing.end(), 0.0);
    ringIdx = 0; hopsSeen = 0;
    std::fill(tpRingL, tpRingL + kTpTaps, 0.0);
    std::fill(tpRingR, tpRingR + kTpTaps, 0.0);
    tpIdx = 0;
    truePeakAll = samplePeakAll = 0.0;
    clipSamples = 0;
    std::fill(fftRing.begin(), fftRing.end(), 0.0);
    fftFill = 0;
    std::fill(psdAvg.begin(), psdAvg.end(), 0.0);
    std::fill(psdSum.begin(), psdSum.end(), 0.0);
    psdPrimed = false;
    psdBlocks = 0;
    psdSignalBlocks = 0;
}

// --- streaming --------------------------------------------------------------

template <typename T>
void AnalyzerCore::processT(const T* L, const T* R, int n)
{
    for (int i = 0; i < n; ++i) {
        // Sanitise at the boundary. A single non-finite sample from an
        // upstream plugin used to poison psdSum for the rest of the session:
        // tilt went NaN, which reached policy as a NaN shelf target and came
        // out the other side as NaN audio. Meters treat garbage as silence.
        double l = (double)L[i];
        double r = (double)R[i];
        if (! std::isfinite(l)) l = 0.0;
        if (! std::isfinite(r)) r = 0.0;
        const double mono = 0.5 * (l + r);

        const double kl = kL.tick(l);
        const double kr = kR.tick(r);
        const double km = kM.tick(mono);
        hopPowSt   += kl * kl + kr * kr;
        hopPowMono += 2.0 * km * km;     // mono fold played on both channels

        const double al = std::fabs(l), ar = std::fabs(r);
        const double a = al > ar ? al : ar;
        if (a > hopPeak) hopPeak = a;
        if (a >= 1.0) ++clipSamples;
        hopRawPow += 0.5 * (l * l + r * r);

        // true peak: 4 interpolation phases per channel
        tpRingL[tpIdx] = l;
        tpRingR[tpIdx] = r;
        for (int ph = 0; ph < 4; ++ph) {
            double accL = 0.0, accR = 0.0;
            int idx = tpIdx;
            for (int t = 0; t < kTpTaps; ++t) {
                accL += tpPhase[ph][t] * tpRingL[idx];
                accR += tpPhase[ph][t] * tpRingR[idx];
                idx = idx == 0 ? kTpTaps - 1 : idx - 1;
            }
            const double tp = std::max(std::fabs(accL), std::fabs(accR));
            if (tp > hopTpPeak) hopTpPeak = tp;
        }
        tpIdx = tpIdx + 1 == kTpTaps ? 0 : tpIdx + 1;

        // spectrum ring (mono)
        fftRing[(size_t)fftFill] = mono;
        if (++fftFill == kFft) {
            accumulateSpectrum();
            // 50 % hop: keep the newer half
            std::copy(fftRing.begin() + kFft / 2, fftRing.end(), fftRing.begin());
            fftFill = kFft / 2;
        }

        if (++hopFill == hopSamples) pushHop();
    }
}

void AnalyzerCore::process(const float* l, const float* r, int n)  { processT(l, r, n); }
void AnalyzerCore::process(const double* l, const double* r, int n){ processT(l, r, n); }

void AnalyzerCore::pushHop()
{
    const double inv = 1.0 / (double)hopSamples;
    stPowRing[(size_t)ringIdx]  = hopPowSt * inv;
    stMonoRing[(size_t)ringIdx] = hopPowMono * inv;
    stPeakRing[(size_t)ringIdx] = hopPeak;
    stRmsRing[(size_t)ringIdx]  = hopRawPow * inv;
    stTpRing[(size_t)ringIdx]   = hopTpPeak;
    ringIdx = (ringIdx + 1) % kStHops;
    ++hopsSeen;

    if (hopTpPeak > truePeakAll) truePeakAll = hopTpPeak;
    if (hopPeak > samplePeakAll) samplePeakAll = hopPeak;

    // 400 ms gating block = mean of the last 4 hops (75 % overlap)
    if (hopsSeen >= 4) {
        double m = 0.0;
        for (int k = 0; k < 4; ++k)
            m += stPowRing[(size_t)((ringIdx - 1 - k + kStHops) % kStHops)];
        gateBlocks.push_back(m / 4.0);
    }

    hopFill = 0;
    hopPowSt = hopPowMono = hopPeak = hopRawPow = hopTpPeak = 0.0;
}

// --- spectrum ---------------------------------------------------------------

// Iterative radix-2 complex FFT, real input. 8192 points every 4096 samples
// is far below audio-thread budgets, and the core may also be run offline.
static void fftRadix2(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / (double)len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

void AnalyzerCore::accumulateSpectrum()
{
    auto& buf = fftScratch;
    double framePow = 0.0;
    for (int i = 0; i < kFft; ++i) {
        const double v = fftRing[(size_t)i] * hannWin[(size_t)i];
        buf[(size_t)i] = v;
        framePow += v * v;
    }
    // -80 dB mean square: below that a frame is a fade tail or silence and
    // earns no vote on spectral readiness (see psdSignalBlocks in the header).
    if (framePow > 1e-8 * (double)kFft)
        ++psdSignalBlocks;
    fftRadix2(buf);

    // ~4 s time constant at one block per 4096 samples
    const double alpha = psdPrimed
        ? std::exp(-(double)(kFft / 2) / (4.0 * sr)) : 0.0;
    for (int k = 0; k <= kFft / 2; ++k) {
        const double p = std::norm(buf[(size_t)k]);
        psdAvg[(size_t)k] = alpha * psdAvg[(size_t)k] + (1.0 - alpha) * p;
        psdSum[(size_t)k] += p;
    }
    psdPrimed = true;
    ++psdBlocks;
}

// --- readout ----------------------------------------------------------------

static inline double lu(double meanPower)
{
    return -0.691 + 10.0 * std::log10(meanPower + 1e-30);
}

Readout AnalyzerCore::readout() const
{
    Readout r;
    const int have = std::min(hopsSeen, kStHops);

    // short-term (stereo + mono fold)
    if (have > 0) {
        double sSt = 0.0, sMono = 0.0, pk = 0.0, rms = 0.0, tp = 0.0;
        for (int k = 0; k < have; ++k) {
            const size_t idx = (size_t)((ringIdx - 1 - k + kStHops) % kStHops);
            sSt += stPowRing[idx];
            sMono += stMonoRing[idx];
            if (stPeakRing[idx] > pk) pk = stPeakRing[idx];
            rms += stRmsRing[idx];
            if (stTpRing[idx] > tp) tp = stTpRing[idx];
        }
        r.lufsShortTerm = lu(sSt / have);
        const double stMono = lu(sMono / have);
        r.monoLossDb = r.lufsShortTerm - stMono;
        r.crestDb = 20.0 * std::log10(pk + 1e-30)
                  - 10.0 * std::log10(rms / have + 1e-30);
        r.truePeakRecentDb = 20.0 * std::log10(tp + 1e-30);
    }

    // integrated with two-stage gating
    if (!gateBlocks.empty()) {
        double sum = 0.0; int cnt = 0;
        for (double p : gateBlocks)
            if (lu(p) > kAbsGateLu) { sum += p; ++cnt; }
        if (cnt > 0) {
            const double relThresh = lu(sum / cnt) + kRelGateLu;
            double sum2 = 0.0; int cnt2 = 0;
            for (double p : gateBlocks)
                if (lu(p) > kAbsGateLu && lu(p) > relThresh) { sum2 += p; ++cnt2; }
            if (cnt2 > 0) {
                r.lufsIntegrated = lu(sum2 / cnt2);
                r.loudnessReady = true;
            }
        }
    }

    r.truePeakDb  = 20.0 * std::log10(truePeakAll + 1e-30);
    r.samplePeakDb= 20.0 * std::log10(samplePeakAll + 1e-30);
    r.clipCount   = (int)std::min<long long>(clipSamples, INT32_MAX);

    // tilt + band shares from the averaged PSD — gated on frames that had
    // signal in them, so an instance that has only heard silence reports no
    // spectrum rather than the "flat, therefore bright" tilt of nothing.
    if (psdSignalBlocks >= 2) {
        const double binHz = sr / (double)kFft;
        auto meanDb = [&](double lo, double hi) {
            double s = 0.0; int c = 0;
            for (int k = 0; k <= kFft / 2; ++k) {
                const double f = k * binHz;
                if (f >= lo && f <= hi) { s += 10.0 * std::log10(psdSum[(size_t)k] + 1e-30); ++c; }
            }
            return c ? s / c : -300.0;
        };
        r.tiltDb = meanDb(2000, 12000) - meanDb(100, 500);

        static const double edges[7] = {20, 60, 150, 400, 2000, 6000, 16000};
        double tot = 0.0, band[6] = {};
        for (int k = 0; k <= kFft / 2; ++k) {
            const double f = k * binHz;
            if (f < 20 || f > 16000) continue;
            tot += psdSum[(size_t)k];
            for (int b = 0; b < 6; ++b)
                if (f >= edges[b] && f < edges[b + 1]) { band[b] += psdSum[(size_t)k]; break; }
        }
        for (int b = 0; b < 6; ++b) r.bandShare[b] = tot > 0 ? band[b] / tot : 0.0;
        r.subShare = r.bandShare[0];
        r.spectrumReady = true;
    }

    // verdicts (pipeline thresholds), all gated on there being a signal:
    // silence measures crest -300 and tilt 0, which would light "already
    // dense" and "bright" on an empty track.
    r.signalPresent = have > 0 && r.lufsShortTerm > kAbsGateLu;
    r.subClass = r.subShare > 0.40 ? Readout::SubHeavy
               : r.subShare > 0.15 ? Readout::SubLeaning : Readout::SubBalanced;
    r.preClipped   = r.truePeakDb > 0.0;
    r.alreadyDense = r.signalPresent && r.crestDb < 9.0;
    r.monoFragile  = r.signalPresent && r.monoLossDb > 2.0;

    // targets vs profile (audio units; the low-shelf and width ladders are
    // the pipeline's, expressed as plain numbers)
    const double li = r.loudnessReady ? r.lufsIntegrated : r.lufsShortTerm;
    r.targets.driveDeltaDb   = profile.driveLufs - li;
    r.targets.gainToTargetDb = profile.targetLufs - li;
    r.targets.tiltDeltaDb    = std::clamp(profile.tiltTarget - r.tiltDb, 0.0, 6.0);
    r.targets.subTrimDb = r.subShare > 0.5  ? -4.0
                        : r.subShare > 0.25 ? -2.5
                        : r.subShare > 0.15 ? -1.5 : 0.0;
    r.targets.widthFactor = r.monoLossDb > 4.0 ? 0.6
                          : r.monoLossDb > 2.0 ? 0.75 : 1.0;
    r.targets.ceilingDbtp = profile.ceilingDbtp;
    return r;
}

} // namespace ferment::analyzer

/* ========================================
 *  Ferment Analyzer — analyzer-core
 *
 *  Realtime measurement core. Speaks audio units only —
 *  dB, LUFS, shares, deltas. It does not know what a knob is; translating
 *  targets into Ferment module values is ferment-policy's job
 *  (FermentPolicy.h), and applying them is Ferment Master's.
 *
 *  Pure C++ (no JUCE): the same core runs in the Analyzer plugin, in
 *  Ferment Master's brain, and headless on iOS.
 *
 * ======================================== */

#ifndef FERMENT_ANALYZER_CORE_H
#define FERMENT_ANALYZER_CORE_H

#include <cstdint>
#include <complex>
#include <vector>

namespace ferment::analyzer {

// Everything the UI (or Master) ever reads. Plain data, cheap to copy.
struct Readout {
    // --- measurements ---------------------------------------------------
    double lufsIntegrated  = -100.0;  // BS.1770 gated, since reset
    double lufsShortTerm   = -100.0;  // 3 s
    double truePeakDb      = -100.0;  // running max, 4x estimate
    double truePeakRecentDb= -100.0;  // 3 s max
    double samplePeakDb    = -100.0;
    int    clipCount       = 0;       // samples at/over full scale
    double crestDb         = 0.0;     // 3 s peak - rms
    double tiltDb          = 0.0;     // mean dB 2-12k minus 100-500
    double bandShare[6]    = {};      // 20-60, 60-150, 150-400, .4-2k, 2-6k, 6-16k
    double monoLossDb      = 0.0;     // ST stereo minus ST of mono fold
    bool   spectrumReady   = false;   // enough audio for tilt/shares
    bool   loudnessReady   = false;   // enough gated blocks for LUFS-I
    /** Short-term loudness above the BS.1770 absolute gate (-70 LUFS).
        Silence measures crest -300 and tilt 0, both of which read as verdicts
        ("already dense", "bright") if taken at face value — so verdicts and
        their consumers gate on this instead of inventing thresholds. */
    bool   signalPresent   = false;

    // --- verdicts (thresholds mirror the offline pipeline) ---------------
    enum SubClass { SubBalanced = 0, SubLeaning, SubHeavy };
    SubClass subClass      = SubBalanced;
    bool preClipped        = false;   // TP > 0 dBTP on the source
    bool alreadyDense      = false;   // crest < 9: limited mastering headroom
    bool monoFragile       = false;   // mono loss > 2 dB
    double subShare        = 0.0;     // convenience: bandShare[0]

    // --- targets vs the active profile (deltas, audio units) -------------
    struct Targets {
        double driveDeltaDb    = 0.0; // profile drive point - LUFS-I
        double gainToTargetDb  = 0.0; // profile loudness target - LUFS-I
        double tiltDeltaDb     = 0.0; // profile tilt target - tilt (>0 = brighten)
        double subTrimDb       = 0.0; // low-shelf ladder from sub share (<= 0)
        double widthFactor     = 1.0; // width ladder from mono loss
        double ceilingDbtp     = -1.0;
    } targets;
};

// Profile = a shipped voicing's *audio* goals (no module values here).
struct Profile {
    const char* name  = "studio";
    double targetLufs = -9.5;
    double ceilingDbtp= -1.5;
    double tiltTarget = -15.0;
    double driveLufs  = -16.0;
};

Profile profileStudio();
Profile profileReel();

class AnalyzerCore {
public:
    void prepare(double sampleRate);
    void reset();

    void setProfile(const Profile& p) { profile = p; }
    const Profile& activeProfile() const { return profile; }

    // Interleaved-agnostic: push per-channel pointers, any block size.
    void process(const float* left, const float* right, int numSamples);
    void process(const double* left, const double* right, int numSamples);

    // Compute-on-demand; call from any thread that owns pacing (UI timer).
    Readout readout() const;

private:
    template <typename T>
    void processT(const T* L, const T* R, int n);

    // --- K-weighting (per lane: L, R, mono fold) -------------------------
    struct Biquad {
        double b0=1, b1=0, b2=0, a1=0, a2=0, z1=0, z2=0;
        inline double tick(double x) {
            const double y = b0*x + z1;
            z1 = b1*x - a1*y + z2;
            z2 = b2*x - a2*y;
            return y;
        }
        void reset() { z1 = z2 = 0; }
    };
    struct KLane {
        Biquad shelf, hp;
        void design(double fs);
        void reset() { shelf.reset(); hp.reset(); }
        inline double tick(double x) { return hp.tick(shelf.tick(x)); }
    };
    KLane kL, kR, kM;

    // --- 100 ms hop accumulators -----------------------------------------
    double sr = 48000.0;
    int hopSamples = 4800, hopFill = 0;
    double hopPowSt = 0.0;     // K-weighted stereo power (L+R)
    double hopPowMono = 0.0;   // K-weighted mono-fold power
    double hopPeak = 0.0;      // plain sample peak
    double hopRawPow = 0.0;    // un-weighted power (crest RMS)
    double hopTpPeak = 0.0;    // true-peak estimate max

    // 400 ms loudness blocks at 75 % overlap = mean of 4 hops.
    std::vector<double> gateBlocks;   // per-block K power (stereo)
    static constexpr int kStHops = 30;  // 3 s of 100 ms hops
    std::vector<double> stPowRing, stMonoRing, stPeakRing, stRmsRing, stTpRing;
    int ringIdx = 0; int hopsSeen = 0;

    // --- true-peak estimator (4x windowed sinc, 24 taps/phase) ------------
    static const int kTpTaps = 24;
    double tpPhase[4][kTpTaps] = {};
    double tpRingL[kTpTaps] = {}, tpRingR[kTpTaps] = {};
    int tpIdx = 0;
    double truePeakAll = 0.0;
    double samplePeakAll = 0.0;
    long long clipSamples = 0;

    // --- spectrum: 8192 Welch, hop 4096, exponential average --------------
    static const int kFft = 8192;
    std::vector<double> fftRing;      // mono samples
    // Sized in prepare(): accumulateSpectrum() used to build a 8192-point
    // complex vector per hop, i.e. a 128 KB malloc/free on the audio thread
    // every 4096 samples (two taps inside Master).
    std::vector<std::complex<double>> fftScratch;
    int fftFill = 0;
    std::vector<double> psdAvg;       // kFft/2+1, exponential (live view)
    std::vector<double> psdSum;       // integrated since reset (verdicts)
    bool psdPrimed = false;
    int psdBlocks = 0;
    /*  Frames with actual signal in them.  spectrumReady gates on this, not on
        psdBlocks: the tilt of silence is 0, which reads as "bright" against
        any musical target — an instance that has only ever heard silence has
        no spectral opinion.  Silent frames still land in psdSum (they add
        nothing), so a track that plays and then stops keeps its verdicts. */
    int psdSignalBlocks = 0;
    std::vector<double> hannWin;

    void pushHop();
    void accumulateSpectrum();

    Profile profile = profileStudio();
};

} // namespace ferment::analyzer

#endif

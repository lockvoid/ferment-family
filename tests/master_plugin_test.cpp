// Ferment Master acceptance test.
//
// Everything here runs through the real FermentMasterProcessor — APVTS in,
// processBlock, parameters out — because everything the wrapper adds lives in
// the gap between the plugin API and MasterCore, and a core-level test cannot
// see any of it.  MasterCore itself is covered by tests/master_test.cpp.
//
//   1. ABI + MAPPING  parameter indices are stable, and every ChainSettings
//                     field arrives at the core in display units, unswapped.
//   2. ALLOCATION     processBlock allocates nothing the core does not.
//   3. LEARN          pressed through the automatable parameter: phase 1 ->
//                     phase 2 -> values in APVTS, gestures balanced, and a
//                     second Learn on identical audio lands identically.
//   4. LIFECYCLE      state restore mid-Learn, prepareToPlay mid-Learn,
//                     over-long blocks, float vs double, a mono buffer, bypass
//                     churn, and latency that never moves.
//   5. BYPASS         all five modules bypassed = the input, delayed by the
//                     latency the plugin reports.
//   6. PUSH           what the loudness push does and does not survive.
//   7. FACE           knob text entry and keyboard nudge on every ModuleCard
//                     knob, and layout at degenerate bounds.

#include "../src-ferment/master/FermentMasterProcessor.h"
#include "../src-ferment/master/FermentMasterEditor.h"

#include <ferment_ui/ferment_ui.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <vector>

// =============================================================================
//  Allocation instrumentation
// =============================================================================
//  Global operator new counts while armed.  The test is single-threaded — the
//  "audio thread" is this one — so the flag is a plain bool and the counter only
//  has to survive being incremented from one place.

namespace alloc
{
    long long count = 0;
    bool armed = false;

    inline void* take (std::size_t bytes)
    {
        if (armed)
            ++count;

        void* p = std::malloc (bytes != 0 ? bytes : 1);

        if (p == nullptr)
            throw std::bad_alloc();

        return p;
    }

    /** Allocations made between here and the matching stop(). */
    inline void start() { count = 0; armed = true; }
    inline long long stop() { armed = false; return count; }
}

void* operator new (std::size_t n) { return alloc::take (n); }
void* operator new[] (std::size_t n) { return alloc::take (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace {

using Proc = FermentMasterProcessor;
namespace FM = ferment::master;

constexpr double kSR    = 48000.0;
constexpr int    kBlock = 512;

int failures = 0;

void check (bool ok, const char* what, const char* detail = "")
{
    if (! ok)
        ++failures;

    std::printf ("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
                 detail[0] ? " — " : "", detail);
}

void checkNear (const char* what, double got, double want, double tol)
{
    char buf[128];
    std::snprintf (buf, sizeof buf, "got %.5f, want %.5f +/- %.5f", got, want, tol);
    check (std::fabs (got - want) <= tol, what, buf);
}

/** The sub-heavy dark fixture from tests/master_test.cpp, verbatim. */
void fixture (std::vector<float>& l, std::vector<float>& r, double seconds)
{
    const int n = (int) (seconds * kSR);
    l.assign ((size_t) n, 0.0f);
    r.assign ((size_t) n, 0.0f);

    unsigned s = 42;
    double lp = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const double t = i / kSR;
        s = s * 1664525u + 1013904223u;
        const double nz = 2.0 * (s / 4294967296.0) - 1.0;
        lp += 0.06 * (nz - lp);

        double x = 0.35 * std::sin (2.0 * M_PI * 45.0 * t)
                 + 0.05 * std::sin (2.0 * M_PI * 300.0 * t)
                 + 0.04 * lp;

        const double ph = std::fmod (t, 0.5);

        if (ph < 0.03)
            x += 0.4 * std::exp (-ph * 200.0) * std::sin (2.0 * M_PI * 70.0 * ph * 30.0);

        l[(size_t) i] = (float) x;
        r[(size_t) i] = (float) (x * 0.98);
    }
}

/** What a host's message thread does between audio callbacks.  The wrapper's
    write-back rides on it, so a test that never pumps is testing a host that
    has hung. */
void pump (int milliseconds = 1)
{
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        mm->runDispatchLoopUntil (milliseconds);
}

double paramValue (Proc& p, Proc::Param index)
{
    auto* raw = p.apvts.getRawParameterValue (Proc::paramIDs()[index]);
    return raw != nullptr ? (double) raw->load() : 0.0;
}

juce::RangedAudioParameter* param (Proc& p, Proc::Param index)
{
    return dynamic_cast<juce::RangedAudioParameter*> (
        p.apvts.getParameter (Proc::paramIDs()[index]));
}

void setParam (Proc& p, Proc::Param index, double displayValue)
{
    if (auto* rp = param (p, index))
    {
        rp->beginChangeGesture();
        rp->setValueNotifyingHost (rp->convertTo0to1 ((float) displayValue));
        rp->endChangeGesture();
    }
}

/** Streams a fixture through the plugin, pumping the message loop as a host
    does, and optionally keeping what came out. */
void run (Proc& p, const std::vector<float>& l, const std::vector<float>& r,
          std::vector<float>* outL = nullptr, std::vector<float>* outR = nullptr)
{
    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;

    if (outL != nullptr) outL->assign (l.size(), 0.0f);
    if (outR != nullptr) outR->assign (r.size(), 0.0f);

    int sincePump = 0;

    for (int pos = 0; pos + kBlock <= (int) l.size(); pos += kBlock)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            buffer.setSample (0, i, l[(size_t) (pos + i)]);
            buffer.setSample (1, i, r[(size_t) (pos + i)]);
        }

        p.processBlock (buffer, midi);

        if (outL != nullptr)
            for (int i = 0; i < kBlock; ++i)
            {
                (*outL)[(size_t) (pos + i)] = buffer.getSample (0, i);
                (*outR)[(size_t) (pos + i)] = buffer.getSample (1, i);
            }

        if (++sincePump >= 8)
        {
            sincePump = 0;
            pump();
        }
    }

    // Let anything the last blocks handed to the message thread land.
    for (int i = 0; i < 30; ++i)
        pump (5);
}

/** Presses Learn the way the face does: a rising edge on the automatable
    parameter, dropped again a block later, and nothing else.  There is no
    back door — if the parameter path is broken, every Learn test fails. */
void pressLearn (Proc& p)
{
    setParam (p, Proc::kLearn, 1.0);
}

void releaseLearn (Proc& p)
{
    setParam (p, Proc::kLearn, 0.0);
}

/** Runs a whole Learn: press, stream the fixture (>= 10 s of phase one plus the
    core's ~4 s phase two), pumping throughout. */
void runLearn (Proc& p, const std::vector<float>& l, const std::vector<float>& r)
{
    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;

    pressLearn (p);

    int blocks = 0, sincePump = 0;

    for (int pos = 0; pos + kBlock <= (int) l.size(); pos += kBlock)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            buffer.setSample (0, i, l[(size_t) (pos + i)]);
            buffer.setSample (1, i, r[(size_t) (pos + i)]);
        }

        p.processBlock (buffer, midi);

        if (++blocks == 2)
            releaseLearn (p);           // the face drops it on its next tick

        if (++sincePump >= 8)
        {
            sincePump = 0;
            pump();
        }
    }

    for (int i = 0; i < 40; ++i)
        pump (5);
}

/** Every parameter Learn is expected to write. */
constexpr int kFirstLearned = Proc::kStageGain;
constexpr int kLastLearned  = Proc::kPushDb;

struct GestureLog : juce::AudioProcessorListener
{
    std::array<int, Proc::kNumParams> begins {}, ends {}, changes {}, depth {};
    int strayEnd = 0;

    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int i) override
    {
        if (valid (i)) { ++begins[(size_t) i]; ++depth[(size_t) i]; }
    }

    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int i) override
    {
        if (! valid (i))
            return;

        ++ends[(size_t) i];

        if (--depth[(size_t) i] < 0)
        {
            ++strayEnd;
            depth[(size_t) i] = 0;
        }
    }

    void audioProcessorParameterChanged (juce::AudioProcessor*, int i, float) override
    {
        if (valid (i)) ++changes[(size_t) i];
    }

    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}

    static bool valid (int i) { return i >= 0 && i < Proc::kNumParams; }
};

// =============================================================================
//  1. Parameter ABI and the APVTS -> core mapping
// =============================================================================

/** One row per ChainSettings field: the parameter that carries it, a value
    unlike every other value in the table (so a swapped pair cannot hide), and
    where it has to arrive. */
struct MapRow
{
    Proc::Param param;
    const char* field;
    double value;
    double ferment::policy::ChainSettings::* asDouble;
    int ferment::policy::ChainSettings::* asInt;
};

void testParameterAbiAndMapping()
{
    Proc p;
    p.prepareToPlay (kSR, kBlock);

    // ---- indices never reorder -------------------------------------------
    // Cuts iOS and the offline pipelines address these by index, so the enum,
    // the id table and the order APVTS reports must be one thing.
    int mismatched = 0;

    for (int i = 0; i < Proc::kNumParams; ++i)
    {
        auto* rp = p.apvts.getParameter (Proc::paramIDs()[i]);

        if (rp == nullptr || rp->getParameterIndex() != i)
            ++mismatched;
    }

    char buf[192];
    std::snprintf (buf, sizeof buf, "%d of %d parameters out of place",
                   mismatched, (int) Proc::kNumParams);
    check (mismatched == 0, "every parameter id sits at its enum index", buf);

    std::snprintf (buf, sizeof buf, "%d exposed, %d in the enum",
                   p.getParameters().size(), (int) Proc::kNumParams);
    check (p.getParameters().size() == Proc::kNumParams,
           "the processor exposes exactly the enumerated parameters", buf);

    // ---- every field, distinct, in display units --------------------------
    struct Want { Proc::Param param; const char* name; double value; };

    const Want wants[] = {
        { Proc::kStageGain,        "stagingDb",          -7.25 },
        { Proc::kStageWidth,       "widthFactor",         0.42 },
        { Proc::kChargeCompression,"charge.compression",  2.25 },
        { Proc::kChargeAttack,     "charge.attack",       3.75 },
        { Proc::kChargeRelease,    "charge.release",      6.25 },
        { Proc::kChargeSaturation, "charge.saturation",   8.75 },
        { Proc::kChargeSatMode,    "charge.satMode",      2.0  },
        { Proc::kChargeCharacter,  "charge.character",    9.25 },
        { Proc::kChargeCharMode,   "charge.charMode",     0.0  },
        { Proc::kChargeHp,         "charge.detectorHp",   1.0  },
        { Proc::kChargeMix,        "charge.mixPct",      37.0  },
        { Proc::kToneLow,          "eq.lowShelfDb",      -6.5  },
        { Proc::kTonePresence,     "eq.presenceDb",       2.5  },
        { Proc::kTonePresenceQ,    "eq.presenceQ",        1.75 },
        { Proc::kToneHigh,         "eq.highShelfDb",      4.5  },
        { Proc::kClipCeiling,      "clip.ceilingDb",     -3.5  },
        { Proc::kClipKnee,         "clip.kneePct",       22.0  },
        { Proc::kClipTilt,         "clip.tiltPct",       77.0  },
        { Proc::kClipBias,         "clip.biasPct",      -41.0  },
        { Proc::kLimitCeiling,     "limit.ceilingDbtp",  -2.25 },
        { Proc::kLimitAttack,      "limit.attackMs",    123.0  },
        { Proc::kLimitRelease,     "limit.releaseScale",  2.5  },
        { Proc::kLimitTruePeak,    "limit.truePeak",      0.0  },
    };

    for (auto& w : wants)
        setParam (p, w.param, w.value);

    std::vector<float> l, r;
    fixture (l, r, 0.05);
    run (p, l, r);

    const auto& s = p.appliedSettings();

    // Read back in the same order as the table, so a swap shows up as two
    // failures naming each other rather than as one puzzling number.
    const double got[] = {
        s.stagingDb, s.widthFactor,
        s.charge.compression, s.charge.attack, s.charge.release, s.charge.saturation,
        (double) s.charge.satMode, s.charge.character, (double) s.charge.charMode,
        (double) s.charge.detectorHp, s.charge.mixPct,
        s.eq.lowShelfDb, s.eq.presenceDb, s.eq.presenceQ, s.eq.highShelfDb,
        s.clip.ceilingDb, s.clip.kneePct, s.clip.tiltPct, s.clip.biasPct,
        s.limit.ceilingDbtp, s.limit.attackMs, s.limit.releaseScale,
        s.limit.truePeak ? 1.0 : 0.0,
    };

    static_assert (sizeof got / sizeof got[0] == sizeof wants / sizeof wants[0],
                   "every ChainSettings field needs a row");

    int wrong = 0;

    for (size_t i = 0; i < sizeof wants / sizeof wants[0]; ++i)
    {
        auto* rp = param (p, wants[i].param);
        const auto range = rp->getNormalisableRange();

        // A float parameter stores a normalised float, so the display value
        // comes back within a float ulp of the range, not exactly.
        const double tol = (range.end - range.start) * 1.0e-5 + 1.0e-6;

        if (std::fabs (got[i] - wants[i].value) > tol)
        {
            ++wrong;
            std::snprintf (buf, sizeof buf, "%s: set %.4f via %s, core has %.4f",
                           wants[i].name, wants[i].value,
                           Proc::paramIDs()[wants[i].param], got[i]);
            check (false, "ChainSettings field round-trip", buf);
        }
    }

    std::snprintf (buf, sizeof buf, "%d of %d fields",
                   (int) (sizeof wants / sizeof wants[0]) - wrong,
                   (int) (sizeof wants / sizeof wants[0]));
    check (wrong == 0, "every ChainSettings field reaches the core in display units", buf);

    // ---- the loudness target is the profile's, not a knob -----------------
    checkNear ("studio profile carries its loudness target",
               s.targetLufs, ferment::analyzer::profileStudio().targetLufs, 1e-9);

    setParam (p, Proc::kProfile, 1.0);         // Reel
    run (p, l, r);

    checkNear ("switching profile moves the loudness target",
               p.appliedSettings().targetLufs,
               ferment::analyzer::profileReel().targetLufs, 1e-9);

    setParam (p, Proc::kProfile, 0.0);

    // ---- bypasses land on the right modules -------------------------------
    int wrongBypass = 0;

    for (int m = 0; m < FM::kNumModules; ++m)
    {
        for (int other = 0; other < FM::kNumModules; ++other)
            setParam (p, Proc::bypassParamFor ((FM::Module) other), other == m ? 1.0 : 0.0);

        run (p, l, r);

        for (int other = 0; other < FM::kNumModules; ++other)
            if (p.moduleBypassed ((FM::Module) other) != (other == m))
                ++wrongBypass;
    }

    for (int m = 0; m < FM::kNumModules; ++m)
        setParam (p, Proc::bypassParamFor ((FM::Module) m), 0.0);

    std::snprintf (buf, sizeof buf, "%d wrong module states over %d combinations",
                   wrongBypass, FM::kNumModules);
    check (wrongBypass == 0, "each bypass parameter bypasses only its own module", buf);
}

// =============================================================================
//  2. Allocation on the audio thread
// =============================================================================

void testAudioThreadAllocations()
{
    Proc p;
    p.prepareToPlay (kSR, kBlock);

    std::vector<float> l, r;
    fixture (l, r, 3.0);

    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;

    const int blocks = (int) l.size() / kBlock;

    auto fill = [&] (int pos)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            buffer.setSample (0, i, l[(size_t) (pos + i)]);
            buffer.setSample (1, i, r[(size_t) (pos + i)]);
        }
    };

    /*  Counting is armed around processBlock ONLY.  A host writing an automation
        lane allocates on its own account, and charging that to the plugin would
        hide the very thing this is looking for.
    */
    auto streamCounting = [&] (Proc& proc, bool automateEveryBlock)
    {
        long long total = 0;

        for (int b = 0; b < blocks; ++b)
        {
            if (automateEveryBlock)
                setParam (proc, Proc::kChargeSaturation, 2.0 + 6.0 * ((b % 16) / 16.0));

            fill (b * kBlock);

            const bool counting = b >= 8;      // warm-up builds rings and scratch

            if (counting) alloc::start();
            proc.processBlock (buffer, midi);
            if (counting) total += alloc::stop();
        }

        return total;
    };

    const long long viaPlugin = streamCounting (p, false);

    // A knob under automation moves every block, which is when the parameter
    // sync actually does its work -- and where a per-block string lookup would
    // sit.  Fresh instance so the spectrum hops line up with the run above.
    Proc automated;
    automated.prepareToPlay (kSR, kBlock);

    const long long viaAutomation = streamCounting (automated, true);

    // The same audio straight through MasterCore, so what the WRAPPER adds is a
    // subtraction rather than an assertion about somebody else's code.
    FM::MasterCore bare;
    bare.prepare (kSR, kBlock);

    std::vector<double> dl ((size_t) kBlock), dr ((size_t) kBlock);

    auto fillDouble = [&] (int pos)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            dl[(size_t) i] = l[(size_t) (pos + i)];
            dr[(size_t) i] = r[(size_t) (pos + i)];
        }
    };

    long long viaCoreTotal = 0;

    for (int b = 0; b < blocks; ++b)
    {
        fillDouble (b * kBlock);

        const bool counting = b >= 8;

        if (counting) alloc::start();
        bare.process (dl.data(), dr.data(), kBlock);

        // The wrapper reads these on the meter cadence; charge them to the core.
        if (b % 20 == 0) { (void) bare.sourceReadout(); (void) bare.resultReadout(); }

        if (counting) viaCoreTotal += alloc::stop();
    }

    const long long viaCore = viaCoreTotal;
    const int counted = blocks - 8;

    char buf[192];
    std::snprintf (buf, sizeof buf,
                   "%lld over %d blocks through the plugin, %lld through MasterCore alone",
                   viaPlugin, counted, viaCore);
    check (viaPlugin <= viaCore, "the wrapper adds no allocation of its own", buf);

    std::snprintf (buf, sizeof buf,
                   "%lld with a knob moving every block, %lld with nothing moving",
                   viaAutomation, viaCore);
    check (viaAutomation <= viaCore, "automating a knob every block adds none either", buf);

    /*  What is left is the core's, and it is not zero: AnalyzerCore builds an
        8192-point std::vector<std::complex<double>> for every spectrum hop and
push_backs a gating block every 400 ms, both from inside process(). That
        allocation was real and is now fixed: accumulateSpectrum() uses a
        scratch buffer sized in prepare(), and gateBlocks reserves an hour of
        capacity. This is no longer a pinned gap but the invariant itself —
        nothing in the chain may allocate on the audio thread.
    */
    std::snprintf (buf, sizeof buf, "%lld allocations over %d blocks", viaCore, counted);
    check (viaCore == 0,
           "the whole chain allocates nothing on the audio thread", buf);

    /*  A block larger than the one prepareToPlay promised must not add any: the
        wrapper chunks through its scratch instead of resizing it.  Both sides
        start fresh so the spectrum hops line up and the counts are comparable
        exactly rather than approximately.
    */
    const int overlong = kBlock * 4;
    const int overlongBlocks = 8;

    Proc oversized;
    oversized.prepareToPlay (kSR, kBlock);

    juce::AudioBuffer<float> big (2, overlong);
    big.clear();

    oversized.processBlock (big, midi);            // warm

    alloc::start();

    for (int i = 0; i < overlongBlocks; ++i)
        oversized.processBlock (big, midi);

    const long long viaOverlong = alloc::stop();

    FM::MasterCore bareOversized;
    bareOversized.prepare (kSR, kBlock);

    std::vector<double> zeroL ((size_t) overlong, 0.0), zeroR ((size_t) overlong, 0.0);
    bareOversized.process (zeroL.data(), zeroR.data(), overlong);

    alloc::start();

    for (int i = 0; i < overlongBlocks; ++i)
        bareOversized.process (zeroL.data(), zeroR.data(), overlong);

    const long long overlongViaCore = alloc::stop();

    std::snprintf (buf, sizeof buf, "%lld through the plugin, %lld through the core, %d blocks of %d",
                   viaOverlong, overlongViaCore, overlongBlocks, overlong);
    check (viaOverlong <= overlongViaCore,
           "an over-long block adds no allocation of the wrapper's own", buf);
}

// =============================================================================
//  3. Learn, end to end, through the parameter
// =============================================================================

void testLearnThroughTheParameter()
{
    Proc p;
    p.prepareToPlay (kSR, kBlock);

    GestureLog log;
    p.addListener (&log);

    std::vector<float> l, r;
    fixture (l, r, 20.0);          // 10 s of phase one, then phase two's 4 s

    // Phase reporting, sampled as the face samples it.
    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;
    char buf[192];

    pressLearn (p);

    bool sawPhase1 = false, sawPhase2 = false;
    double progressAtFiveSeconds = -1.0;
    double mixDuringWriteback = -1.0;
    int blocks = 0, sincePump = 0;

    for (int pos = 0; pos + kBlock <= (int) l.size(); pos += kBlock)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            buffer.setSample (0, i, l[(size_t) (pos + i)]);
            buffer.setSample (1, i, r[(size_t) (pos + i)]);
        }

        p.processBlock (buffer, midi);

        if (++blocks == 2)
            releaseLearn (p);

        const auto status = p.learnStatus();

        if (status.phase == 1) sawPhase1 = true;

        /*  The instant phase two starts, the learned values are in the core but
            not yet on the knobs.  A knob moved in that window must not drag the
            whole pre-Learn chain back into the core with it -- that is what the
            write-back gate is for, and this is the only moment it is visible.
        */
        if (status.phase == 2 && ! sawPhase2)
        {
            sawPhase2 = true;
            setParam (p, Proc::kChargeMix, 5.0);
            p.processBlock (buffer, midi);
            mixDuringWriteback = p.appliedSettings().charge.mixPct;
        }

        if (progressAtFiveSeconds < 0.0 && pos >= (int) (5.0 * kSR))
            progressAtFiveSeconds = status.progress;

        if (++sincePump >= 8)
        {
            sincePump = 0;
            pump();
        }
    }

    for (int i = 0; i < 40; ++i)
        pump (5);

    check (sawPhase1, "pressing the learn parameter starts phase one");
    check (sawPhase2, "phase two runs after the wrapper finishes phase one");
    check (p.learnStatus().phase == 0, "Learn returns to idle");

    std::snprintf (buf, sizeof buf, "core held %.1f while the knobs still said otherwise",
                   mixDuringWriteback);
    check (std::fabs (mixDuringWriteback - 60.0) < 0.05,
           "a knob moved before the write-back lands cannot undo the Learn", buf);

    std::snprintf (buf, sizeof buf, "%.2f after 5 s of a %.0f s listen",
                   progressAtFiveSeconds, Proc::learnListenSeconds);
    check (progressAtFiveSeconds > 0.35 && progressAtFiveSeconds < 0.65,
           "phase one reports progress against its listen window", buf);

    /*  ferment-policy's studio sub-heavy row, as pinned by master_test — the
        point being that those values reach the VISIBLE parameters, which is what
        the knobs, the host's automation lanes and the saved session all read.
    */
    checkNear ("charge_satmode = Mild",       paramValue (p, Proc::kChargeSatMode),     0.0, 1e-6);
    checkNear ("charge_hp = 300 Hz",          paramValue (p, Proc::kChargeHp),          2.0, 1e-6);
    checkNear ("charge_mix = 60%",            paramValue (p, Proc::kChargeMix),        60.0, 0.05);
    checkNear ("charge_saturation = 3.5",     paramValue (p, Proc::kChargeSaturation),  3.5, 0.01);
    checkNear ("charge_compression = 5.5",    paramValue (p, Proc::kChargeCompression), 5.5, 0.01);
    checkNear ("tone_low = -4.0 dB",          paramValue (p, Proc::kToneLow),          -4.0, 0.02);
    checkNear ("clip_ceiling = -1.0 dB",      paramValue (p, Proc::kClipCeiling),      -1.0, 0.02);
    checkNear ("limit_ceiling = -1.5 dBTP",   paramValue (p, Proc::kLimitCeiling),     -1.5, 0.02);

    const double high = paramValue (p, Proc::kToneHigh);
    std::snprintf (buf, sizeof buf, "tone_high = %.2f dB", high);
    check (high > 3.0 && high <= 6.0 + 1e-6, "tilt shelf strongly engaged on the dark fixture", buf);

    const double push = paramValue (p, Proc::kPushDb);
    std::snprintf (buf, sizeof buf, "push_db = %.2f dB", push);
    check (std::fabs (push) > 0.5, "the learned loudness push reached push_db", buf);

    // ---- what APVTS holds is what the core is running ----------------------
    const auto& applied = p.appliedSettings();

    checkNear ("core staging matches stage_gain",
               applied.stagingDb, paramValue (p, Proc::kStageGain), 0.01);
    checkNear ("core width matches stage_width",
               applied.widthFactor, paramValue (p, Proc::kStageWidth), 0.001);
    checkNear ("core charge mix matches charge_mix",
               applied.charge.mixPct, paramValue (p, Proc::kChargeMix), 0.01);
    checkNear ("core clip knee matches clip_knee",
               applied.clip.kneePct, paramValue (p, Proc::kClipKnee), 0.01);
    checkNear ("core clip tilt matches clip_tilt",
               applied.clip.tiltPct, paramValue (p, Proc::kClipTilt), 0.01);

    // ---- gestures ---------------------------------------------------------
    int unwrapped = 0, unbalanced = 0, totalBegins = 0, totalEnds = 0;

    for (int i = kFirstLearned; i <= kLastLearned; ++i)
    {
        totalBegins += log.begins[(size_t) i];
        totalEnds   += log.ends[(size_t) i];

        if (log.begins[(size_t) i] == 0 || log.changes[(size_t) i] == 0)
            ++unwrapped;

        if (log.begins[(size_t) i] != log.ends[(size_t) i])
            ++unbalanced;
    }

    std::snprintf (buf, sizeof buf, "%d of %d learned parameters never gestured",
                   unwrapped, kLastLearned - kFirstLearned + 1);
    check (unwrapped == 0, "Learn writes every value it owns through the host", buf);

    std::snprintf (buf, sizeof buf, "%d begins, %d ends, %d parameters unbalanced, %d stray ends",
                   totalBegins, totalEnds, unbalanced, log.strayEnd);
    check (unbalanced == 0 && log.strayEnd == 0 && totalBegins == totalEnds,
           "every gesture Learn opens is closed", buf);

    // Learn does not touch what it does not own.
    int touched = 0;

    for (int i = Proc::kBypassStage; i <= Proc::kBypassLimit; ++i)
        touched += log.begins[(size_t) i];

    std::snprintf (buf, sizeof buf, "%d gestures on bypass parameters", touched);
    check (touched == 0, "Learn leaves the bypasses alone", buf);

    p.removeListener (&log);

    // ---- the learned chain lands on target --------------------------------
    p.reset();

    std::vector<float> l2, r2;
    fixture (l2, r2, 12.0);
    run (p, l2, r2);

    const auto snap = p.snapshot();
    std::snprintf (buf, sizeof buf, "result %.2f LUFS", snap.resultLufs);
    check (std::fabs (snap.resultLufs - ferment::analyzer::profileStudio().targetLufs) <= 1.0,
           "learned chain renders within 1 LUFS of the studio target", buf);
}

void testLearnIsRepeatable()
{
    Proc p;
    p.prepareToPlay (kSR, kBlock);

    std::vector<float> l, r;
    fixture (l, r, 20.0);

    runLearn (p, l, r);

    std::array<double, Proc::kNumParams> first {};

    for (int i = kFirstLearned; i <= kLastLearned; ++i)
        first[(size_t) i] = paramValue (p, (Proc::Param) i);

    // Same audio, same profile, same core — MasterCore is deterministic by
    // design, so a second Learn must not drift.
    p.reset();
    runLearn (p, l, r);

    int drifted = 0;
    double worst = 0.0;
    const char* worstId = "";

    for (int i = kFirstLearned; i < Proc::kPushDb; ++i)
    {
        const double delta = std::fabs (paramValue (p, (Proc::Param) i) - first[(size_t) i]);

        if (delta > worst) { worst = delta; worstId = Proc::paramIDs()[i]; }
        if (delta > 1.0e-5) ++drifted;
    }

    char buf[192];
    std::snprintf (buf, sizeof buf, "%d parameters drifted, worst %.6g on %s",
                   drifted, worst, worstId[0] ? worstId : "-");
    check (drifted == 0, "a second Learn on identical audio lands identically", buf);

    // The push is measured on the already-coloured signal, so it is allowed a
    // little slack where the colour knobs are not.
    const double pushDelta = std::fabs (paramValue (p, Proc::kPushDb) - first[(size_t) Proc::kPushDb]);
    std::snprintf (buf, sizeof buf, "%.3f dB between runs", pushDelta);
    check (pushDelta < 0.75, "the loudness push repeats to within a fraction of a dB", buf);
}

void testLearnSurvivesAMomentaryPress()
{
    /*  A host with a long buffer can call processBlock once every 170 ms, and
        the face drops the Learn parameter 50 ms after raising it.  If the
        wrapper only samples the parameter per block, that press is never seen.
    */
    Proc p;
    const int longBlock = 8192;
    p.prepareToPlay (kSR, longBlock);

    juce::AudioBuffer<float> buffer (2, longBlock);
    juce::MidiBuffer midi;
    buffer.clear();

    p.processBlock (buffer, midi);

    pressLearn (p);
    releaseLearn (p);                  // the whole press falls between two blocks

    p.processBlock (buffer, midi);
    pump();

    char buf[128];
    std::snprintf (buf, sizeof buf, "phase %d after the press", p.learnStatus().phase);
    check (p.learnStatus().phase == 1, "a press shorter than one block still starts Learn", buf);
}

// =============================================================================
//  4. Hostile lifecycle
// =============================================================================

void testRestoringASessionDoesNotStartALearn()
{
    Proc saver;
    saver.prepareToPlay (kSR, kBlock);

    // The host saves inside the 50 ms the face holds the button up.
    pressLearn (saver);

    juce::MemoryBlock state;
    saver.getStateInformation (state);

    Proc loaded;
    loaded.prepareToPlay (kSR, kBlock);
    loaded.setStateInformation (state.getData(), (int) state.getSize());

    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;
    buffer.clear();
    loaded.processBlock (buffer, midi);
    pump();

    char buf[128];
    std::snprintf (buf, sizeof buf, "phase %d after loading a session saved mid-press",
                   loaded.learnStatus().phase);
    check (loaded.learnStatus().phase == 0, "loading a session never starts a Learn", buf);
}


void testStateRestoreDuringLearn()
{
    Proc p;
    p.prepareToPlay (kSR, kBlock);

    // A saved session with a knob nowhere near what Learn will choose.
    setParam (p, Proc::kChargeMix, 12.0);

    juce::MemoryBlock saved;
    p.getStateInformation (saved);

    std::vector<float> l, r;
    fixture (l, r, 20.0);

    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;

    pressLearn (p);

    int blocks = 0, sincePump = 0;

    for (int pos = 0; pos + kBlock <= (int) l.size(); pos += kBlock)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            buffer.setSample (0, i, l[(size_t) (pos + i)]);
            buffer.setSample (1, i, r[(size_t) (pos + i)]);
        }

        p.processBlock (buffer, midi);

        if (++blocks == 2)
            releaseLearn (p);

        // Five seconds in — the middle of phase one.
        if (pos == (int) (5.0 * kSR) / kBlock * kBlock)
            p.setStateInformation (saved.getData(), (int) saved.getSize());

        if (++sincePump >= 8) { sincePump = 0; pump(); }
    }

    for (int i = 0; i < 40; ++i)
        pump (5);

    char buf[160];
    std::snprintf (buf, sizeof buf, "phase %d, charge_mix %.1f",
                   p.learnStatus().phase, paramValue (p, Proc::kChargeMix));
    check (p.learnStatus().phase == 0, "a state restore mid-Learn does not strand the state machine", buf);
    checkNear ("Learn wins over the state it was restored into",
               paramValue (p, Proc::kChargeMix), 60.0, 0.05);
    checkNear ("the core is running what the parameters say",
               p.appliedSettings().charge.mixPct, paramValue (p, Proc::kChargeMix), 0.01);
}

void testPrepareDuringLearn()
{
    Proc p;
    p.prepareToPlay (kSR, kBlock);

    std::vector<float> l, r;
    fixture (l, r, 3.0);

    pressLearn (p);

    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;

    for (int pos = 0; pos + kBlock <= (int) l.size(); pos += kBlock)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            buffer.setSample (0, i, l[(size_t) (pos + i)]);
            buffer.setSample (1, i, r[(size_t) (pos + i)]);
        }

        p.processBlock (buffer, midi);
    }

    releaseLearn (p);
    pump();

    check (p.learnStatus().phase == 1, "Learn is running before the graph changes");

    const int latencyAt48k = p.getLatencySamples();

    // The host re-plans the graph: new rate, new block size, mid-Learn.
    p.prepareToPlay (44100.0, 1024);
    pump();

    char buf[160];
    std::snprintf (buf, sizeof buf, "phase %d after prepareToPlay(44100, 1024)",
                   p.learnStatus().phase);
    check (p.learnStatus().phase == 0,
           "a sample-rate change cancels Learn and says so", buf);

    // And the plugin still works afterwards.
    juce::AudioBuffer<float> other (2, 1024);
    other.clear();
    p.processBlock (other, midi);

    std::snprintf (buf, sizeof buf, "%d samples at 48k, %d at 44.1k",
                   latencyAt48k, p.getLatencySamples());
    check (p.getLatencySamples() > 0, "latency is reported at the new rate", buf);
}

void testOverlongBlocksAndPrecision()
{
    const int prepared = 256;
    const int oversize = prepared * 8;

    std::vector<float> l, r;
    fixture (l, r, 2.0);

    // A: the host honours the block size it promised.
    Proc a;
    a.prepareToPlay (kSR, prepared);

    juce::AudioBuffer<float> small (2, prepared);
    juce::MidiBuffer midi;
    std::vector<float> outA;
    outA.assign (l.size(), 0.0f);

    for (int pos = 0; pos + prepared <= (int) l.size(); pos += prepared)
    {
        for (int i = 0; i < prepared; ++i)
        {
            small.setSample (0, i, l[(size_t) (pos + i)]);
            small.setSample (1, i, r[(size_t) (pos + i)]);
        }

        a.processBlock (small, midi);

        for (int i = 0; i < prepared; ++i)
            outA[(size_t) (pos + i)] = small.getSample (0, i);
    }

    // B: the host hands over eight times what it promised.
    Proc b;
    b.prepareToPlay (kSR, prepared);

    juce::AudioBuffer<float> large (2, oversize);
    std::vector<float> outB;
    outB.assign (l.size(), 0.0f);

    for (int pos = 0; pos + oversize <= (int) l.size(); pos += oversize)
    {
        for (int i = 0; i < oversize; ++i)
        {
            large.setSample (0, i, l[(size_t) (pos + i)]);
            large.setSample (1, i, r[(size_t) (pos + i)]);
        }

        b.processBlock (large, midi);

        for (int i = 0; i < oversize; ++i)
            outB[(size_t) (pos + i)] = large.getSample (0, i);
    }

    double worst = 0.0;
    const size_t compared = (size_t) ((int) l.size() / oversize * oversize);

    for (size_t i = 0; i < compared; ++i)
        worst = std::max (worst, (double) std::fabs (outA[i] - outB[i]));

    char buf[160];
    std::snprintf (buf, sizeof buf, "worst deviation %.3e over %d samples",
                   worst, (int) compared);
    check (worst == 0.0, "an over-long block is bit-identical to prepared-size blocks", buf);

    // ---- float against double ---------------------------------------------
    Proc f, d;
    f.prepareToPlay (kSR, kBlock);
    d.setProcessingPrecision (juce::AudioProcessor::doublePrecision);
    d.prepareToPlay (kSR, kBlock);

    check (d.supportsDoublePrecisionProcessing(), "the plugin offers double-precision processing");

    juce::AudioBuffer<float>  fb (2, kBlock);
    juce::AudioBuffer<double> db (2, kBlock);

    double worstPrecision = 0.0;

    for (int pos = 0; pos + kBlock <= (int) l.size(); pos += kBlock)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            fb.setSample (0, i, l[(size_t) (pos + i)]);
            fb.setSample (1, i, r[(size_t) (pos + i)]);
            db.setSample (0, i, (double) l[(size_t) (pos + i)]);
            db.setSample (1, i, (double) r[(size_t) (pos + i)]);
        }

        f.processBlock (fb, midi);
        d.processBlock (db, midi);

        for (int i = 0; i < kBlock; ++i)
            worstPrecision = std::max (worstPrecision,
                                       std::fabs ((double) fb.getSample (0, i) - db.getSample (0, i)));
    }

    std::snprintf (buf, sizeof buf, "worst deviation %.3e", worstPrecision);
    check (worstPrecision < 1.0e-6, "the float and double paths agree to float precision", buf);
}

void testMonoAndOddBuffers()
{
    Proc p;
    p.prepareToPlay (kSR, kBlock);

    juce::AudioProcessor::BusesLayout monoIn;
    monoIn.inputBuses.add (juce::AudioChannelSet::mono());
    monoIn.outputBuses.add (juce::AudioChannelSet::stereo());

    // Master is a stereo bus master chain; the honest answer to mono in is no,
    // and the host then feeds it a stereo pair instead of guessing.
    check (! p.checkBusesLayoutSupported (monoIn),
           "mono in / stereo out is refused rather than silently mishandled");

    juce::AudioProcessor::BusesLayout stereo;
    stereo.inputBuses.add (juce::AudioChannelSet::stereo());
    stereo.outputBuses.add (juce::AudioChannelSet::stereo());
    check (p.checkBusesLayoutSupported (stereo), "stereo in / stereo out is supported");

    // A host that hands over a single-channel buffer anyway must not be a crash
    // and must not be garbage: leaving it alone is the only safe answer.
    juce::AudioBuffer<float> single (1, kBlock);
    juce::MidiBuffer midi;

    for (int i = 0; i < kBlock; ++i)
        single.setSample (0, i, 0.25f);

    p.processBlock (single, midi);

    double worst = 0.0;

    for (int i = 0; i < kBlock; ++i)
        worst = std::max (worst, (double) std::fabs (single.getSample (0, i) - 0.25f));

    char buf[128];
    std::snprintf (buf, sizeof buf, "worst change %.3e", worst);
    check (worst == 0.0, "a one-channel buffer is passed through untouched", buf);

    // An empty block is a real thing hosts do at transport edges.
    juce::AudioBuffer<float> empty (2, 0);
    p.processBlock (empty, midi);
    check (true, "an empty block is a no-op");

    /*  And a block before prepareToPlay, or after releaseResources, is a thing
        hosts do while re-planning the graph.  MasterCore::process() spins
        forever on an unprepared core, so the wrapper has to refuse first.
    */
    Proc unprepared;
    juce::AudioBuffer<float> pending (2, kBlock);

    for (int i = 0; i < kBlock; ++i)
    {
        pending.setSample (0, i, 0.5f);
        pending.setSample (1, i, 0.5f);
    }

    unprepared.processBlock (pending, midi);

    p.releaseResources();
    p.processBlock (pending, midi);

    double moved = 0.0;

    for (int i = 0; i < kBlock; ++i)
        moved = std::max (moved, (double) std::fabs (pending.getSample (0, i) - 0.5f));

    std::snprintf (buf, sizeof buf, "worst change %.3e", moved);
    check (moved == 0.0, "a block before prepareToPlay or after release is refused, not run", buf);
}

void testBypassChurnAndLatency()
{
    /*  A steady tone, not the transient fixture: a click is a discontinuity, and
        on a signal whose own largest step is 0.009 a discontinuity has nowhere
        to hide.  On the burst fixture the audio's own slew swamps it.
    */
    const int seconds = 3;
    std::vector<float> l ((size_t) (seconds * (int) kSR)), r (l.size());

    for (size_t i = 0; i < l.size(); ++i)
    {
        const double t = (double) i / kSR;
        l[i] = (float) (0.3 * std::sin (2.0 * M_PI * 220.0 * t));
        r[i] = (float) (0.3 * std::sin (2.0 * M_PI * 220.0 * t + 0.4));
    }

    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;

    /*  The first second is the chain arriving: a 24 Hz high pass, a 60 ms
        limiter attack and a compressor all settling on a tone that started at
        full level.  Toggling and measuring both begin after it.
    */
    const size_t settled = (size_t) kSR;

    // run() only fills whole blocks; the tail it leaves at zero is the harness,
    // not the plugin, and measuring into it invents a step.
    const size_t measured = (l.size() / (size_t) kBlock) * (size_t) kBlock;

    auto maxStep = [settled, measured] (const std::vector<float>& v)
    {
        double worst = 0.0;

        for (size_t i = settled; i < measured; ++i)
            worst = std::max (worst, (double) std::fabs (v[i] - v[i - 1]));

        return worst;
    };

    // Reference: the chain running normally.
    Proc quiet;
    quiet.prepareToPlay (kSR, kBlock);
    std::vector<float> steady, steadyR;
    run (quiet, l, r, &steady, &steadyR);

    // And now with every module's bypass flipped on every single block.
    Proc churn;
    churn.prepareToPlay (kSR, kBlock);

    const int latencyBefore = churn.getLatencySamples();
    std::vector<float> churned;
    churned.assign (l.size(), 0.0f);

    bool state = false;
    int latencyMoved = 0;

    for (int pos = 0; pos + kBlock <= (int) l.size(); pos += kBlock)
    {
        if ((size_t) pos >= settled)
        {
            state = ! state;

            for (int m = 0; m < FM::kNumModules; ++m)
                setParam (churn, Proc::bypassParamFor ((FM::Module) m), state ? 1.0 : 0.0);
        }

        for (int i = 0; i < kBlock; ++i)
        {
            buffer.setSample (0, i, l[(size_t) (pos + i)]);
            buffer.setSample (1, i, r[(size_t) (pos + i)]);
        }

        churn.processBlock (buffer, midi);

        if (churn.getLatencySamples() != latencyBefore)
            ++latencyMoved;

        for (int i = 0; i < kBlock; ++i)
            churned[(size_t) (pos + i)] = buffer.getSample (0, i);
    }

    // The other end of the crossfade, so the claim can say what a click WOULD
    // have measured rather than only what it did not.
    Proc dry;
    dry.prepareToPlay (kSR, kBlock);

    for (int m = 0; m < FM::kNumModules; ++m)
        setParam (dry, Proc::bypassParamFor ((FM::Module) m), 1.0);

    std::vector<float> dryOut, dryOutR;
    run (dry, l, r, &dryOut, &dryOutR);

    double gap = 0.0;

    for (size_t i = settled; i < measured; ++i)
        gap = std::max (gap, (double) std::fabs (steady[i] - dryOut[i]));

    /*  The core crossfades over 10 ms, so switching cannot add more than one
        fade step of movement per sample on top of whatever the audio was already
        doing.  That is the bound, derived rather than eyeballed — and `gap` is
        the size of the jump a missing crossfade would have produced instead.
    */
    const double fadeStep = 2.0 / (0.010 * kSR);
    const double reference = maxStep (steady);
    const double churnedStep = maxStep (churned);

    char buf[192];
    std::snprintf (buf, sizeof buf,
                   "steady %.4f, churned %.4f, fade step %.4f, wet/dry gap being crossed %.4f",
                   reference, churnedStep, fadeStep, gap);
    check (churnedStep <= reference + fadeStep && gap > 10.0 * churnedStep,
           "toggling every bypass every block never steps the output", buf);

    std::snprintf (buf, sizeof buf, "%d blocks reported a different latency than %d",
                   latencyMoved, latencyBefore);
    check (latencyMoved == 0 && latencyBefore > 0,
           "latency never moves when modules are bypassed", buf);

    // And the number the plugin reports is the number the core computes.
    FM::MasterCore bare;
    bare.prepare (kSR, kBlock);

    std::snprintf (buf, sizeof buf, "plugin %d, core %d", latencyBefore, bare.latencySamples());
    check (latencyBefore == bare.latencySamples(),
           "reported latency is MasterCore's latency", buf);
}

// =============================================================================
//  5. All-bypass null
// =============================================================================

void testAllBypassIsDelayedInput()
{
    Proc p;
    p.prepareToPlay (kSR, kBlock);

    for (int m = 0; m < FM::kNumModules; ++m)
        setParam (p, Proc::bypassParamFor ((FM::Module) m), 1.0);

    std::vector<float> l, r, outL, outR;
    fixture (l, r, 4.0);
    run (p, l, r, &outL, &outR);

    const int latency = p.getLatencySamples();
    double worst = 0.0;

    // Skipping the first second lets the 10 ms bypass crossfades finish; the
    // claim is about the steady state, and the fades have their own test above.
    for (size_t i = (size_t) latency + 48000; i < outL.size(); ++i)
        worst = std::max (worst, (double) std::fabs (outL[i] - l[i - (size_t) latency]));

    char buf[128];
    std::snprintf (buf, sizeof buf, "worst deviation %.3e at latency %d", worst, latency);
    check (latency > 0 && worst < 1.0e-6,
           "all-bypass equals the input delayed by the reported latency", buf);
}

void testNonFiniteInputCannotPoisonTheChain()
{
    /*  One NaN sample from an upstream plugin used to lodge in the recursive
        state of the engines — filters, envelopes, the limiter's averagers —
        and the chain output NaN for the rest of the session.  The wrapper now
        sanitises at the boundary; the injected burst must come out as a
        dropout that ends, not a poisoning that does not.
    */
    Proc p;
    p.prepareToPlay (kSR, kBlock);

    auto sine = [] (double seconds)
    {
        std::vector<float> v ((size_t) (seconds * kSR));
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                             * 220.0 * (double) i / kSR);
        return v;
    };

    auto clean = sine (2.0);
    run (p, clean, clean);

    auto poisoned = sine (1.0);
    for (size_t i = 4000; i < 4064; ++i)
        poisoned[i] = std::numeric_limits<float>::quiet_NaN();
    poisoned[8000] = std::numeric_limits<float>::infinity();
    run (p, poisoned, poisoned);

    auto after = sine (2.0);
    std::vector<float> outL, outR;
    run (p, after, after, &outL, &outR);

    int nonFinite = 0;
    double peak = 0.0;
    for (size_t i = 0; i < outL.size(); ++i)
    {
        if (! std::isfinite (outL[i]) || ! std::isfinite (outR[i]))
            ++nonFinite;
        peak = std::max (peak, (double) std::fabs (outL[i]));
    }

    char buf[96];
    std::snprintf (buf, sizeof buf, "%d non-finite samples, peak %.3f", nonFinite, peak);
    check (nonFinite == 0 && peak > 0.01 && peak < 4.0,
           "a NaN/Inf burst passes through as a dropout, not a poisoning", buf);
}

// =============================================================================
//  6. Automation and what the push survives
// =============================================================================

void testAutomationAndPushAfterReload()
{
    Proc p;
    p.prepareToPlay (kSR, kBlock);

    std::vector<float> l, r;
    fixture (l, r, 20.0);
    runLearn (p, l, r);

    const double learnedSaturation = paramValue (p, Proc::kChargeSaturation);
    const double learnedPush = paramValue (p, Proc::kPushDb);

    // A host moves a knob after Learn.  It must take, and it must reach the core.
    setParam (p, Proc::kChargeSaturation, 8.0);

    std::vector<float> l2, r2;
    fixture (l2, r2, 1.0);
    run (p, l2, r2);

    char buf[192];
    std::snprintf (buf, sizeof buf, "learned %.2f, host set 8.0, parameter %.2f, core %.2f",
                   learnedSaturation, paramValue (p, Proc::kChargeSaturation),
                   p.appliedSettings().charge.saturation);
    check (std::fabs (paramValue (p, Proc::kChargeSaturation) - 8.0) < 0.01
             && std::fabs (p.appliedSettings().charge.saturation - 8.0) < 0.01,
           "a host edit after Learn reaches the core and is not swept back", buf);

    juce::MemoryBlock state;
    p.getStateInformation (state);

    Proc reloaded;
    reloaded.prepareToPlay (kSR, kBlock);
    reloaded.setStateInformation (state.getData(), (int) state.getSize());

    checkNear ("charge_saturation survives save/load",
               paramValue (reloaded, Proc::kChargeSaturation), 8.0, 0.01);
    checkNear ("charge_mix survives save/load",
               paramValue (reloaded, Proc::kChargeMix), paramValue (p, Proc::kChargeMix), 0.01);
    checkNear ("push_db itself is saved and restored",
               paramValue (reloaded, Proc::kPushDb), learnedPush, 0.01);

    // Render the same fixture through both, meters cleared, so each number
    // describes only its own render.
    p.reset();

    std::vector<float> l3, r3;
    fixture (l3, r3, 12.0);
    run (p, l3, r3);
    const double liveLufs = p.snapshot().resultLufs;

    run (reloaded, l3, r3);
    const double reloadedLufs = reloaded.snapshot().resultLufs;

    /*  The bug this pins: push_db restored into APVTS but never re-applied to
        the core, so a reopened session came back quieter by exactly the
        learned push.  MasterCore::setPushDb plus the sync branch is the fix;
        both processors now render the same fixture at the same loudness.
    */
    std::snprintf (buf, sizeof buf, "live %.2f LUFS, reloaded %.2f LUFS, gap %.2f dB",
                   liveLufs, reloadedLufs, liveLufs - reloadedLufs);
    check (std::fabs (liveLufs - reloadedLufs) < 0.25,
           "the learned push is re-applied on reload", buf);
}

// =============================================================================
//  6b. Push as a drivable parameter
// =============================================================================

void testPushParameterDrivesTheCore()
{
    /*  Push is applied at the Clip input, after Charge — so on a signal quiet
        enough that neither ceiling engages, +6 dB of push is exactly +6 dB of
        output, and the two renders differ by nothing else.
    */
    auto render = [] (double pushDb)
    {
        Proc p;
        p.prepareToPlay (kSR, kBlock);
        setParam (p, Proc::kPushDb, pushDb);

        std::vector<float> l ((size_t) (2.0 * kSR)), r;
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = 0.03f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                             * 220.0 * (double) i / kSR);
        r = l;

        std::vector<float> outL, outR;
        run (p, l, r, &outL, &outR);

        double acc = 0.0;
        const size_t tail = outL.size() / 2;        // skip the fade-in
        for (size_t i = tail; i < outL.size(); ++i)
            acc += (double) outL[i] * (double) outL[i];
        return 10.0 * std::log10 (acc / (double) tail + 1e-30);
    };

    const double flat   = render (0.0);
    const double pushed = render (6.0);

    char buf[96];
    std::snprintf (buf, sizeof buf, "push 0 -> %.2f dB, push 6 -> %.2f dB", flat, pushed);
    check (std::fabs ((pushed - flat) - 6.0) < 0.1,
           "the push parameter is a real gain, not a readout", buf);
}

// =============================================================================
//  7. The face
// =============================================================================

template <typename T>
void collect (juce::Component& root, std::vector<T*>& found)
{
    if (auto* hit = dynamic_cast<T*> (&root))
    {
        found.push_back (hit);
        return;                       // a knob never contains another knob
    }

    for (auto* child : root.getChildren())
        if (child != nullptr)
            collect (*child, found);
}

void testTheFace()
{
    Proc p;
    p.prepareToPlay (kSR, kBlock);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());

    std::vector<ferment::FermentKnob*> knobs;
    collect (*editor, knobs);

    char buf[192];
    std::snprintf (buf, sizeof buf, "found %d for %d module parameters",
                   (int) knobs.size(), Proc::kPushDb - Proc::kStageGain);
    check ((int) knobs.size() == Proc::kPushDb - Proc::kStageGain,
           "every module parameter has a knob on a card", buf);

    /*  The rack scrolls precisely so that nothing has to be shrunk to fit, so
        "every face is the family's standard size" is the property that says the
        layout is doing its job.
    */
    int wrongSize = 0, smallest = 9999, largest = 0;

    for (auto* knob : knobs)
    {
        const int face = knob->getSlider().getWidth();
        smallest = juce::jmin (smallest, face);
        largest  = juce::jmax (largest, face);

        if (face != ferment::FermentKnob::standardFaceSize)
            ++wrongSize;
    }

    std::snprintf (buf, sizeof buf, "faces run %d..%d px, standard is %d",
                   smallest, largest, ferment::FermentKnob::standardFaceSize);
    check (wrongSize == 0, "every knob is the family's standard face size", buf);

    std::vector<ferment::ModuleCard*> moduleCards;
    collect (*editor, moduleCards);

    std::snprintf (buf, sizeof buf, "%d cards for %d modules",
                   (int) moduleCards.size(), FM::kNumModules);
    check ((int) moduleCards.size() == FM::kNumModules, "one card per module", buf);

    if (knobs.size() > 1)
    {
        const int cell = knobs[1]->getX() - knobs[0]->getX();
        std::snprintf (buf, sizeof buf, "cell %d px = face %d + gutter %d",
                       cell, ferment::FermentKnob::standardFaceSize, ferment::FermentKnob::gutter);
        check (cell == ferment::FermentKnob::standardFaceSize + ferment::FermentKnob::gutter,
               "knobs sit one gutter apart", buf);
    }

    for (auto* child : editor->getChildren())
        if (auto* viewport = dynamic_cast<juce::Viewport*> (child))
        {
            const auto* content = viewport->getViewedComponent();
            std::snprintf (buf, sizeof buf, "rack is %d px wide in a %d px window",
                           content != nullptr ? content->getWidth() : 0,
                           viewport->getMaximumVisibleWidth());
            check (content != nullptr && content->getWidth() > viewport->getMaximumVisibleWidth(),
                   "the rack is wider than its window, so it scrolls", buf);
        }

    int textFailures = 0, nudgeFailures = 0;

    for (auto* knob : knobs)
    {
        auto& slider = knob->getSlider();
        const double lo = slider.getMinimum(), hi = slider.getMaximum();
        const double step = slider.getInterval() > 0.0 ? slider.getInterval() : (hi - lo) * 0.02;

        /*  Round trip through the knob's OWN display format: typing "0.33" into
            a knob that prints percent should land on 0.33%, so a test that typed
            raw slider values would be asserting the wrong thing.  What has to
            hold is that whatever the face prints, the face can read back.
        */
        const double target = lo + (hi - lo) / 3.0;

        slider.setValue (target, juce::sendNotificationSync);
        const auto printed = knob->displayText();
        const double landed = slider.getValue();

        slider.setValue (lo + (hi - lo) * 0.8, juce::sendNotificationSync);

        if (! knob->setValueFromText (printed) || std::fabs (slider.getValue() - landed) > step)
            ++textFailures;

        /*  Arrow key: a real KeyPress through the real slider.  Both steps start
            from the bottom of the range, the one place every knob — including a
            two-state one — has room to step up.
        */
        slider.setValue (lo, juce::sendNotificationSync);
        const double start = slider.getValue();

        slider.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        const double fineDelta = slider.getValue() - start;

        if (fineDelta <= 0.0)
            ++nudgeFailures;

        slider.setValue (start, juce::sendNotificationSync);
        slider.keyPressed (juce::KeyPress (juce::KeyPress::upKey,
                                           juce::ModifierKeys::shiftModifier, 0));
        const double coarseDelta = slider.getValue() - start;

        if (coarseDelta < fineDelta)
            ++nudgeFailures;

        // Strictly further only where there is room: a three-position mode knob
        // is already one fine step from its top.
        if (start + 10.0 * fineDelta <= hi && coarseDelta <= fineDelta)
            ++nudgeFailures;
    }

    std::snprintf (buf, sizeof buf, "%d of %d knobs rejected a typed value",
                   textFailures, (int) knobs.size());
    check (textFailures == 0, "text entry works on every ModuleCard knob", buf);

    std::snprintf (buf, sizeof buf, "%d arrow-key failures across %d knobs",
                   nudgeFailures, (int) knobs.size());
    check (nudgeFailures == 0, "arrows and shift-arrows nudge every ModuleCard knob", buf);

    /*  A host handing an editor a one-pixel rectangle mid-resize drives the
        card row's shared-width arithmetic negative; painting is what exercises it.
    */
    const juce::Point<int> sizes[] = {
        { 1, 1 }, { 2, 2 }, { 1, 900 }, { 900, 1 }, { 60, 60 }, { 4000, 3000 }
    };

    for (auto size : sizes)
    {
        editor->setSize (size.x, size.y);
        pump (5);

        juce::Image image (juce::Image::ARGB, size.x, size.y, true);
        juce::Graphics g (image);
        editor->paintEntireComponent (g, false);
    }

    check (true, "lays out and paints at degenerate and oversized bounds");
}

/** The face is one client of the wrapper, not the wrapper's owner: a Learn run
    with an editor open has to land in exactly the same place. */
void testLearnWithTheFaceOpen()
{
    Proc p;
    p.prepareToPlay (kSR, kBlock);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());

    std::vector<float> l, r;
    fixture (l, r, 20.0);
    runLearn (p, l, r);

    char buf[160];
    std::snprintf (buf, sizeof buf, "phase %d, charge_mix %.1f, push %.2f",
                   p.learnStatus().phase, paramValue (p, Proc::kChargeMix),
                   paramValue (p, Proc::kPushDb));
    check (p.learnStatus().phase == 0, "Learn completes with an editor open", buf);
    checkNear ("the same values land with the face open",
               paramValue (p, Proc::kChargeMix), 60.0, 0.05);
    check (std::fabs (paramValue (p, Proc::kPushDb)) > 0.5,
           "the push lands with the face open", buf);
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf ("Ferment Master plugin tests\n\n");

    std::printf ("1. parameter ABI and mapping:\n");
    testParameterAbiAndMapping();

    std::printf ("2. audio-thread allocation:\n");
    testAudioThreadAllocations();

    std::printf ("3. Learn:\n");
    testLearnThroughTheParameter();
    testLearnIsRepeatable();
    testLearnSurvivesAMomentaryPress();

    std::printf ("4. hostile lifecycle:\n");
    testRestoringASessionDoesNotStartALearn();
    testStateRestoreDuringLearn();
    testPrepareDuringLearn();
    testOverlongBlocksAndPrecision();
    testMonoAndOddBuffers();
    testBypassChurnAndLatency();
    testNonFiniteInputCannotPoisonTheChain();

    std::printf ("5. bypass:\n");
    testAllBypassIsDelayedInput();

    std::printf ("6. automation and the loudness push:\n");
    testAutomationAndPushAfterReload();
    testPushParameterDrivesTheCore();

    std::printf ("7. the face:\n");
    testTheFace();
    testLearnWithTheFaceOpen();

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures,
                 failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

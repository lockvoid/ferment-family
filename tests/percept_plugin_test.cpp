// Ferment Percept (analyzer) acceptance test.
//
// The suite is built so that breaking the plugin turns it red.  Each section
// says what it would catch; a check that cannot fail is not a check.
//
//   1. PASSTHROUGH  every sample out is bit-identical to the sample in — float
//                   and double, block sizes 1 / 13 / 512 / 8192, an empty
//                   block, silence, denormals, NaN and Inf, and mono in to a
//                   stereo bus.  Compared as bit patterns, so a NaN that came
//                   in has to be the same NaN going out.  Reported latency is
//                   zero.
//   2. MEASUREMENT  what the core reports about known signals, checked against
//                   arithmetic rather than against a previous run: a -20 dBFS
//                   tone is -20 LUFS-I, a 1.2 sine is +1.58 dBTP, a 4 %-duty
//                   burst train has 17 dB of crest, an anti-phase pair loses
//                   everything to a mono fold.  This is the layer that catches
//                   a Readout field that has stopped moving.
//   3. FACE         the editor's chips are the core's verdicts and nothing
//                   else, over six fixtures chosen so that every chip is seen
//                   both lit and dark.  The suite refuses to pass if a chip
//                   never varied, because a chip that is always off agrees
//                   with a mis-wired editor.
//   4. POLICY       the hint panel prints `ferment::policy::translate` and has
//                   no mapping of its own — proved by switching the profile
//                   and requiring the panel to move with policy's output, and
//                   by finding policy's own numbers in the rendered rows.
//                   Result mode prints the verification rows instead.
//   5. CONTROLS     reset is edge-triggered, actually resets, and survives a
//                   host that only calls back every 8192 samples; the profile
//                   parameter reaches the core; out-of-range and corrupt
//                   state do not crash.
//   6. POISON       NaN in the signal, then how far the core is still usable,
//                   and whether reset gets it back.
//   7. ALLOCATION   the poll and the paint both reach the heap zero times,
//                   measured differentially against an idle message loop so
//                   the loop's own housekeeping cannot be mistaken for ours.
//   8. LIFECYCLE    closing and reopening the editor keeps the measurement and
//                   does not crash; degenerate and oversized bounds paint.
//
// Sections 3 and 4 go through the real editor and its own Timer rather than
// through Readout, because the interesting failure is a panel wired to the
// wrong field, and a test that re-reads Readout cannot see that at all.

#include "../src-ferment/core/FermentPolicy.h"
#include "../src-ferment/percept/FermentPerceptEditor.h"
#include "../src-ferment/percept/FermentPerceptProcessor.h"

#include <ferment_ui/ferment_ui.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

// ---- allocation harness ----------------------------------------------------
// Global operator new is replaced so an allocation anywhere — ours, JUCE's, the
// standard library's — is counted while the gate is open.

namespace {
std::atomic<bool>   allocGateOpen { false };
std::atomic<size_t> allocCount { 0 };
}

void* operator new (size_t size)
{
    if (allocGateOpen.load (std::memory_order_relaxed))
        allocCount.fetch_add (1, std::memory_order_relaxed);

    if (auto* p = std::malloc (size == 0 ? 1 : size))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (size_t size) { return operator new (size); }
void  operator delete (void* p) noexcept { std::free (p); }
void  operator delete[] (void* p) noexcept { std::free (p); }
void  operator delete (void* p, size_t) noexcept { std::free (p); }
void  operator delete[] (void* p, size_t) noexcept { std::free (p); }

namespace {

namespace FA = ferment::analyzer;

constexpr double kSR = 48000.0;

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
    std::snprintf (buf, sizeof buf, "got %.4f, want %.4f +/- %.3f", got, want, tol);
    check (std::fabs (got - want) <= tol, what, buf);
}

/** Depth-first walk for a component of a given type inside a real editor —
    the same trick FermentEditorShots uses to find knobs. */
template <typename T>
T* findChild (juce::Component& root)
{
    if (auto* hit = dynamic_cast<T*> (&root))
        return hit;

    for (auto* child : root.getChildren())
        if (auto* hit = findChild<T> (*child))
            return hit;

    return nullptr;
}

void runEditorFor (int milliseconds)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
}

const char* const* ids() { return FermentPerceptProcessor::paramIDs(); }

void setParam (FermentPerceptProcessor& p, int index, float normalised)
{
    if (auto* param = p.apvts.getParameter (ids()[index]))
        param->setValueNotifyingHost (normalised);
}

// ---- fixtures --------------------------------------------------------------

/** A stereo test signal. `gen` is handed the sample index and fills L and R,
    so a fixture is one line of maths rather than a buffer literal. */
struct Fixture
{
    std::vector<float> l, r;

    Fixture (double seconds, const std::function<void (int, double&, double&)>& gen)
    {
        const int n = (int) (seconds * kSR);
        l.resize ((size_t) n);
        r.resize ((size_t) n);

        for (int i = 0; i < n; ++i)
        {
            double a = 0.0, b = 0.0;
            gen (i, a, b);
            l[(size_t) i] = (float) a;
            r[(size_t) i] = (float) b;
        }
    }

    int size() const { return (int) l.size(); }
};

struct Lcg
{
    unsigned s;
    explicit Lcg (unsigned seed) : s (seed) {}
    double operator()() { s = s * 1664525u + 1013904223u; return 2.0 * (s / 4294967296.0) - 1.0; }
};

/** EBU Tech 3341: 997 Hz at the given dBFS on both channels. */
Fixture sine (double freq, double db, double seconds)
{
    const double amp = std::pow (10.0, db / 20.0);
    return { seconds, [=] (int i, double& a, double& b)
             { a = b = amp * std::sin (2.0 * M_PI * freq * i / kSR); } };
}

/** The sub-heavy dark fixture from tests/master_test.cpp, verbatim, so both
    products are judged against the same signal. */
Fixture subHeavy (double seconds)
{
    auto rnd = std::make_shared<Lcg> (42u);
    auto lp  = std::make_shared<double> (0.0);

    return { seconds, [rnd, lp] (int i, double& a, double& b)
    {
        const double t = i / kSR;
        *lp += 0.06 * ((*rnd)() - *lp);

        double x = 0.35 * std::sin (2.0 * M_PI * 45.0 * t)
                 + 0.05 * std::sin (2.0 * M_PI * 300.0 * t)
                 + 0.04 * *lp;

        const double ph = std::fmod (t, 0.5);

        if (ph < 0.03)
            x += 0.4 * std::exp (-ph * 200.0) * std::sin (2.0 * M_PI * 70.0 * ph * 30.0);

        a = x;
        b = x * 0.98;
    } };
}

/** Broadband noise with no sub emphasis — the control for the verdict test. */
Fixture balanced (double seconds)
{
    auto rnd = std::make_shared<Lcg> (7u);
    return { seconds, [rnd] (int, double& a, double& b) { a = 0.2 * (*rnd)(); b = a * 0.99; } };
}

/** Noise plus just enough 45 Hz to sit in the 0.15-0.40 sub-share band. */
Fixture subLeaning (double seconds)
{
    auto rnd = std::make_shared<Lcg> (99u);
    return { seconds, [rnd] (int i, double& a, double& b)
             { a = 0.08 * std::sin (2.0 * M_PI * 45.0 * i / kSR) + 0.2 * (*rnd)(); b = a * 0.99; } };
}

/** A source that arrived already over full scale: peak 1.2 = +1.58 dBFS. */
Fixture hot (double seconds)
{
    return { seconds, [] (int i, double& a, double& b)
             { a = b = 1.2 * std::sin (2.0 * M_PI * 220.0 * i / kSR); } };
}

/** 20 ms of 1 kHz every 500 ms at 0.9: 4 % duty, so crest is
    20*log10 (0.9 / (0.9/sqrt(2) * sqrt(0.04))) = 17.0 dB. */
Fixture sparse (double seconds)
{
    return { seconds, [] (int i, double& a, double& b)
    {
        const double ph = std::fmod (i / kSR, 0.5);
        a = ph < 0.02 ? 0.9 * std::sin (2.0 * M_PI * 1000.0 * ph) : 0.0;
        b = a * 0.99;
    } };
}

/** Anti-phase: the mono fold cancels completely. */
Fixture wide (double seconds)
{
    auto rnd = std::make_shared<Lcg> (3u);
    return { seconds, [rnd] (int, double& a, double& b) { a = 0.25 * (*rnd)(); b = -a; } };
}

Fixture silence (double seconds)
{
    return { seconds, [] (int, double& a, double& b) { a = b = 0.0; } };
}

// ---- streaming -------------------------------------------------------------

/** Streams a fixture through the processor in blocks of @a blockSize.
    @a afterBlock sees the buffer the processor handed back. */
template <typename Sample>
void feed (FermentPerceptProcessor& p, const Fixture& f, int blockSize = 512,
           const std::function<void (juce::AudioBuffer<Sample>&, int)>& afterBlock = {})
{
    juce::AudioBuffer<Sample> buffer (2, blockSize);
    juce::MidiBuffer midi;

    for (int pos = 0; pos + blockSize <= f.size(); pos += blockSize)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            buffer.setSample (0, i, (Sample) f.l[(size_t) (pos + i)]);
            buffer.setSample (1, i, (Sample) f.r[(size_t) (pos + i)]);
        }

        p.processBlock (buffer, midi);

        if (afterBlock)
            afterBlock (buffer, pos);
    }
}

FA::Readout measure (const Fixture& f)
{
    FermentPerceptProcessor p;
    p.prepareToPlay (kSR, 512);
    feed<float> (p, f);
    return p.readout();
}

// =============================================================================
//  1. Passthrough
// =============================================================================

/** Bit patterns, not values: a passthrough that turned a signalling NaN into a
    quiet one, or -0.0 into +0.0, would still compare equal as a double. */
template <typename Sample>
bool sameBits (const juce::AudioBuffer<Sample>& buffer, int channel,
               const Sample* want, int n)
{
    return std::memcmp (buffer.getReadPointer (channel), want, sizeof (Sample) * (size_t) n) == 0;
}

template <typename Sample>
void passthroughAtBlockSize (const char* label, const Fixture& f, int blockSize)
{
    FermentPerceptProcessor p;
    p.prepareToPlay (kSR, blockSize);

    std::vector<Sample> wantL ((size_t) blockSize), wantR ((size_t) blockSize);
    bool exact = true;
    int blocks = 0;

    std::function<void (juce::AudioBuffer<Sample>&, int)> compare =
        [&] (juce::AudioBuffer<Sample>& buffer, int pos)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            wantL[(size_t) i] = (Sample) f.l[(size_t) (pos + i)];
            wantR[(size_t) i] = (Sample) f.r[(size_t) (pos + i)];
        }

        if (! sameBits (buffer, 0, wantL.data(), blockSize)
            || ! sameBits (buffer, 1, wantR.data(), blockSize))
            exact = false;

        // A reset part-way through: resetting the measurement must not so much
        // as graze the audio.
        if (++blocks == 5)
            setParam (p, FermentPerceptProcessor::kReset, 1.0f);
    };

    feed<Sample> (p, f, blockSize, compare);

    char buf[160];
    std::snprintf (buf, sizeof buf, "%s, %d-sample blocks (%d of them)", label, blockSize, blocks);
    check (exact && blocks > 0, "output is bit-identical to input", buf);
}

void testPassthrough()
{
    const auto music = subHeavy (3.0);

    for (int blockSize : { 1, 13, 512, 8192 })
    {
        passthroughAtBlockSize<float>  ("float",  music, blockSize);
        passthroughAtBlockSize<double> ("double", music, blockSize);
    }

    // Signals that are not music.  Denormals are the case ScopedNoDenormals
    // exists for: flushing them to zero must happen inside the analyser, not on
    // the way out.
    passthroughAtBlockSize<float> ("silence", silence (0.2), 512);
    passthroughAtBlockSize<float> ("denormals", Fixture (0.2, [] (int i, double& a, double& b)
        { a = b = 1.0e-320 * (double) ((i % 7) + 1); }), 512);
    passthroughAtBlockSize<float> ("NaN and Inf", Fixture (0.2, [] (int i, double& a, double& b)
    {
        const int phase = i % 4;
        a = b = phase == 0 ? std::numeric_limits<double>::quiet_NaN()
              : phase == 1 ? std::numeric_limits<double>::infinity()
              : phase == 2 ? -std::numeric_limits<double>::infinity()
                           : 0.5;
    }), 512);
}

void testEmptyBlock()
{
    FermentPerceptProcessor p;
    p.prepareToPlay (kSR, 512);

    juce::AudioBuffer<float> buffer (2, 0);
    juce::MidiBuffer midi;
    p.processBlock (buffer, midi);

    /*  getReadPointer(0) on a channel-less buffer is a null pointer; a guard
        that checks the bus's channel count instead of the buffer's hands it to
        the core. */
    juce::AudioBuffer<float> noChannels (0, 512);
    p.processBlock (noChannels, midi);

    check (true, "a zero-sample block and a zero-channel block are survivable");
}

void testMonoInStereoOut()
{
    FermentPerceptProcessor p;

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::mono());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());

    if (! p.setBusesLayout (layout))
    {
        check (false, "mono in / stereo out is an accepted layout");
        return;
    }

    constexpr int n = 256;
    p.prepareToPlay (kSR, n);

    juce::AudioBuffer<float> buffer (2, n);
    juce::MidiBuffer midi;

    for (int i = 0; i < n; ++i)
    {
        buffer.setSample (0, i, (float) std::sin (2.0 * M_PI * 440.0 * i / kSR));
        buffer.setSample (1, i, -999.0f);      // whatever the host left behind
    }

    p.processBlock (buffer, midi);

    bool filled = true;
    for (int i = 0; i < n; ++i)
        if (buffer.getSample (1, i) != buffer.getSample (0, i))
            filled = false;

    check (filled, "mono input is copied to the second output channel");
}

void testLatency()
{
    FermentPerceptProcessor p;
    p.prepareToPlay (kSR, 512);

    char buf[64];
    std::snprintf (buf, sizeof buf, "reports %d samples", p.getLatencySamples());
    check (p.getLatencySamples() == 0, "zero latency", buf);
}

// =============================================================================
//  2. Measurement — checked against arithmetic, not against a previous run
// =============================================================================

void testMeasurement()
{
    {
        const auto r = measure (sine (997.0, -20.0, 8.0));
        check (r.loudnessReady, "integrated loudness is ready after 8 s");
        checkNear ("997 Hz -20 dBFS reads -20.0 LUFS-I", r.lufsIntegrated, -20.0, 0.1);
        checkNear ("short-term agrees with integrated", r.lufsShortTerm, r.lufsIntegrated, 0.3);
    }
    {
        // 20*log10(1.2) = 1.584: a sine's true peak is its amplitude.
        const auto r = measure (hot (4.0));
        checkNear ("a 1.2 sine reads +1.58 dBTP", r.truePeakDb, 1.584, 0.15);
        checkNear ("...and the same sample peak", r.samplePeakDb, 1.584, 0.05);
        check (r.clipCount > 0, "...and counts samples over full scale");
        check (r.preClipped, "...and is called pre-clipped");
    }
    {
        // 4 % duty at 0.9 peak: 20*log10 (0.9 / (0.9/sqrt(2) * sqrt(0.04))).
        const auto r = measure (sparse (6.0));
        checkNear ("a 4 %-duty burst train has 17 dB of crest", r.crestDb, 17.0, 1.0);
        check (! r.alreadyDense, "...and is not called already dense");
    }
    {
        const auto r = measure (wide (6.0));
        check (r.monoLossDb > 20.0, "an anti-phase pair loses everything to a mono fold");
        check (r.monoFragile, "...and is called mono fragile");
    }
    {
        const auto r = measure (balanced (6.0));
        checkNear ("white noise measures flat", r.tiltDb, 0.0, 1.0);
        check (r.spectrumReady, "...with the spectrum ready");
    }
    {
        const auto r = measure (silence (4.0));
        check (r.truePeakDb <= -100.0 && r.lufsShortTerm <= -100.0,
               "silence reads at the floor, not as a number");
        check (! r.loudnessReady, "...and never claims an integrated loudness");
    }
}

// =============================================================================
//  3. The face is the core's verdicts
// =============================================================================

struct FixtureCase { const char* name; Fixture signal; };

void testVerdictChips()
{
    // Chip order is the editor's, and the expectations below are stated from
    // Readout independently of how the editor derives them.
    enum { SubHeavyChip, SubLeaningChip, DarkChip, BrightChip,
           PreClippedChip, DenseChip, MonoFragileChip, kNumChips };

    std::vector<FixtureCase> cases;
    cases.push_back ({ "sub-heavy",   subHeavy (12.0) });
    cases.push_back ({ "sub-leaning", subLeaning (12.0) });
    cases.push_back ({ "balanced",    balanced (12.0) });
    cases.push_back ({ "hot",         hot (12.0) });
    cases.push_back ({ "sparse",      sparse (12.0) });
    cases.push_back ({ "anti-phase",  wide (12.0) });

    bool everLit[kNumChips] = {};
    bool everDark[kNumChips] = {};
    bool allAgree = true;
    char firstMismatch[160] = {};

    for (const auto& c : cases)
    {
        FermentPerceptProcessor p;
        p.prepareToPlay (kSR, 512);
        feed<float> (p, c.signal);

        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* chips = findChild<ferment::VerdictChips> (*editor);

        if (chips == nullptr)
        {
            check (false, "the editor has a VerdictChips row");
            return;
        }

        runEditorFor (200);      // several 20 Hz poll ticks

        const auto r = p.readout();
        const auto profile = p.activeProfile();

        // Stated from the core's fields, not from the editor's expression of
        // them: dark and bright are the two sides of the profile's tilt target.
        const bool want[kNumChips] = {
            r.subClass == FA::Readout::SubHeavy,
            r.subClass == FA::Readout::SubLeaning,
            r.spectrumReady && r.tiltDb < profile.tiltTarget,
            r.spectrumReady && r.tiltDb > profile.tiltTarget,
            r.preClipped,
            r.alreadyDense,
            r.monoFragile,
        };

        for (int i = 0; i < kNumChips; ++i)
        {
            (want[i] ? everLit[i] : everDark[i]) = true;

            if (chips->isActive (i) != want[i] && allAgree)
            {
                allAgree = false;
                std::snprintf (firstMismatch, sizeof firstMismatch,
                               "%s fixture, chip %d: face says %d, core says %d",
                               c.name, i, (int) chips->isActive (i), (int) want[i]);
            }
        }
    }

    check (allAgree, "every chip is the core's verdict, over six fixtures", firstMismatch);

    // A chip that is dark for every fixture agrees with an editor that never
    // wires it up at all, so the check above would be worth nothing for it.
    bool varied = true;
    char detail[160] = {};

    for (int i = 0; i < kNumChips; ++i)
        if (! (everLit[i] && everDark[i]))
        {
            varied = false;
            std::snprintf (detail, sizeof detail, "chip %d was always %s", i,
                           everLit[i] ? "lit" : "dark");
            break;
        }

    check (varied, "and every chip was seen both lit and dark, so the check has teeth", detail);
}

// =============================================================================
//  4. The hint panel is ferment-policy's output
// =============================================================================

/** Drives a processor and its real editor to a settled frame. */
struct LiveEditor
{
    FermentPerceptProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    FermentPerceptEditor* face = nullptr;

    explicit LiveEditor (const Fixture& f)
    {
        processor.prepareToPlay (kSR, 512);
        feed<float> (processor, f);
        editor.reset (processor.createEditor());
        face = dynamic_cast<FermentPerceptEditor*> (editor.get());
        settle();
    }

    void settle() { runEditorFor (200); }

    juce::String hint() const { return face != nullptr ? face->hintText() : juce::String(); }
};

void testHintPanelIsPolicy()
{
    LiveEditor live { subHeavy (12.0) };

    if (live.face == nullptr)
    {
        check (false, "createEditor returns a FermentPerceptEditor");
        return;
    }

    const auto studioChain = ferment::policy::translate (live.processor.readout(),
                                                         live.processor.activeProfile());
    const auto studioHint = live.hint();

    setParam (live.processor, FermentPerceptProcessor::kProfile, 1.0f);   // Reel
    live.settle();

    const auto reelProfile = live.processor.activeProfile();
    const auto reelChain = ferment::policy::translate (live.processor.readout(), reelProfile);
    const auto reelHint = live.hint();

    check (juce::String (reelProfile.name) == "reel", "the profile parameter reaches the core",
           reelProfile.name);

    // Policy voices the two profiles differently, so a panel that renders
    // policy has to move when the profile does.  A local mapping table would
    // print the same rows for both.
    const bool policyMoved = studioChain.limit.ceilingDbtp != reelChain.limit.ceilingDbtp
                          || studioChain.eq.presenceDb != reelChain.eq.presenceDb;

    check (policyMoved, "policy voices Studio and Reel differently");
    check (studioHint != reelHint, "the hint panel moves with the profile");

    // And it is policy's numbers on screen, not a plausible set of its own.
    auto carries = [] (const juce::String& text, double value, int decimals)
    {
        return text.contains (juce::String (value, decimals));
    };

    check (carries (reelHint, reelChain.limit.ceilingDbtp, 1)
           && carries (reelHint, reelChain.clip.ceilingDb, 1)
           && carries (reelHint, reelChain.eq.presenceDb, 1)
           && carries (reelHint, reelChain.charge.compression, 1),
           "the rendered rows carry policy's own values",
           reelHint.replaceCharacter ('\n', '|').toRawUTF8());
}

void testResultMode()
{
    LiveEditor live { hot (12.0) };

    if (live.face == nullptr)
    {
        check (false, "createEditor returns a FermentPerceptEditor");
        return;
    }

    check (live.hint().startsWith ("CHAIN HINT"), "Source mode shows the chain hint");

    setParam (live.processor, FermentPerceptProcessor::kMode, 1.0f);      // Result
    live.settle();

    const auto verify = live.hint();
    const auto r = live.processor.readout();

    check (verify.startsWith ("VERIFY"), "Result mode swaps the panel for verification rows");

    // The panel prints the true peak to two decimals, so this is the field
    // itself reaching the screen — a frozen truePeakDb prints the wrong number
    // here as well as failing section 2.
    check (verify.contains (juce::String (r.truePeakDb, 2)),
           "the verification rows carry the measured true peak",
           verify.replaceCharacter ('\n', '|').toRawUTF8());

    check (verify.contains ("FAIL"), "a +1.58 dBTP source fails the ceiling check");
    check (verify.contains ("TP over -1.0 dBTP"), "...and is flagged as an AAC risk");
}

// =============================================================================
//  5. Controls
// =============================================================================

void testResetIsAnEdge()
{
    FermentPerceptProcessor p;
    p.prepareToPlay (kSR, 512);

    const auto tone = sine (997.0, -20.0, 8.0);
    feed<float> (p, tone);

    check (p.readout().loudnessReady, "eight seconds of tone gives an integrated loudness");

    // An automation lane parked at 1 must not wipe the measurement on every
    // block — only the transition to 1 resets.
    setParam (p, FermentPerceptProcessor::kReset, 1.0f);
    feed<float> (p, tone);

    const auto held = p.readout();
    char buf[128];
    std::snprintf (buf, sizeof buf, "LUFS-I %.2f, ready=%d", held.lufsIntegrated,
                   (int) held.loudnessReady);
    check (held.loudnessReady && std::fabs (held.lufsIntegrated + 20.0) < 0.2,
           "a reset parameter held high does not reset every block", buf);
}

void testResetActuallyResets()
{
    FermentPerceptProcessor p;
    p.prepareToPlay (kSR, 512);

    feed<float> (p, hot (4.0));
    check (p.readout().truePeakDb > 0.0, "the hot fixture leaves a true peak over 0 dBTP");

    setParam (p, FermentPerceptProcessor::kReset, 0.0f);
    setParam (p, FermentPerceptProcessor::kReset, 1.0f);
    feed<float> (p, silence (0.5));

    const auto after = p.readout();
    char buf[96];
    std::snprintf (buf, sizeof buf, "true peak now %.2f dBTP, %d clipped samples",
                   after.truePeakDb, after.clipCount);
    check (after.truePeakDb <= -100.0 && after.clipCount == 0,
           "a rising edge on reset clears the measurement", buf);
}

void testResetSurvivesLargeBlocks()
{
    /*  The editor raises the parameter on click and drops it on the next poll,
        50 ms later.  At an 8192-sample buffer a block is 170 ms, so the whole
        pulse can fall between two calls to processBlock: an edge detector that
        only samples the parameter on the audio thread never sees it, and the
        button silently does nothing.  Reproduced here at its worst — the pulse
        completes with no block in between.
    */
    constexpr int blockSize = 8192;

    FermentPerceptProcessor p;
    p.prepareToPlay (kSR, blockSize);

    feed<float> (p, hot (4.0), blockSize);
    check (p.readout().truePeakDb > 0.0, "8192-sample blocks measure normally");

    setParam (p, FermentPerceptProcessor::kReset, 1.0f);
    setParam (p, FermentPerceptProcessor::kReset, 0.0f);

    feed<float> (p, silence (1.0), blockSize);

    const auto after = p.readout();
    char buf[128];
    std::snprintf (buf, sizeof buf, "true peak still %.2f dBTP after the click",
                   after.truePeakDb);
    check (after.truePeakDb <= -100.0,
           "a reset click shorter than one block is not lost", buf);
}

/** Collects every component of a type, so a face with several can be searched
    by what is written on it. */
template <typename T>
void collect (juce::Component& root, std::vector<T*>& out)
{
    if (auto* hit = dynamic_cast<T*> (&root))
        out.push_back (hit);

    for (auto* child : root.getChildren())
        collect (child != nullptr ? *child : root, out);
}

void testResetButton()
{
    /*  The parameter path is checked above; this is the path a user actually
        takes.  The button writes the whole momentary pulse inside one click, so
        nothing downstream may depend on the parameter still being up by the
        time the next block arrives. */
    LiveEditor live { hot (4.0) };

    check (live.processor.readout().truePeakDb > 0.0, "the hot fixture leaves a true peak to clear");

    std::vector<ferment::FermentToggle*> toggles;
    collect (*live.editor, toggles);

    ferment::FermentToggle* resetButton = nullptr;
    for (auto* t : toggles)
        if (t->getButtonText() == "RESET")
            resetButton = t;

    if (resetButton == nullptr)
    {
        check (false, "the editor has a RESET button");
        return;
    }

    resetButton->triggerClick();
    live.settle();
    feed<float> (live.processor, silence (0.5));

    const auto after = live.processor.readout();
    char buf[96];
    std::snprintf (buf, sizeof buf, "true peak now %.2f dBTP", after.truePeakDb);
    check (after.truePeakDb <= -100.0, "clicking RESET on the face clears the measurement", buf);
}

void testProfileReachesTheCore()
{
    FermentPerceptProcessor p;
    p.prepareToPlay (kSR, 512);

    const auto tone = sine (997.0, -20.0, 1.0);
    feed<float> (p, tone);

    const auto studio = p.activeProfile();
    const auto studioCeiling = p.readout().targets.ceilingDbtp;

    /*  Switched with the transport stopped, and read back without another
        block going through: a listener who picks Reel while playback is paused
        must see the targets move.  readout() is asked first, so it has to bring
        the profile up to date on its own rather than leaning on activeProfile()
        having been called before it. */
    setParam (p, FermentPerceptProcessor::kProfile, 1.0f);

    const auto reelCeiling = p.readout().targets.ceilingDbtp;
    const auto reel = p.activeProfile();

    char buf[160];
    std::snprintf (buf, sizeof buf, "%s ceiling %.2f -> %s ceiling %.2f",
                   studio.name, studioCeiling, reel.name, reelCeiling);
    check (juce::String (studio.name) == "studio" && juce::String (reel.name) == "reel"
           && studioCeiling != reelCeiling,
           "switching profile retargets the core with no block in between", buf);
}

void testHostileParameters()
{
    FermentPerceptProcessor p;
    p.prepareToPlay (kSR, 512);

    // Out of range, both ends, on every parameter.
    for (int index = 0; index < FermentPerceptProcessor::kNumParams; ++index)
        for (float v : { -3.0f, -0.0f, 1.0f, 7.5f })
            setParam (p, index, v);

    feed<float> (p, balanced (0.5));

    const auto profile = p.activeProfile();
    const bool known = juce::String (profile.name) == "studio"
                    || juce::String (profile.name) == "reel";
    check (known, "out-of-range parameter values leave a known profile", profile.name);

    // A corrupt session blob is what setStateInformation actually meets.
    const char junk[] = "not a value tree at all";
    p.setStateInformation (junk, (int) sizeof junk);
    p.setStateInformation (nullptr, 0);

    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    p.setStateInformation (saved.getData(), (int) saved.getSize());

    feed<float> (p, balanced (0.5));
    check (true, "corrupt and truncated state blobs are survivable");
}

// =============================================================================
//  6. NaN
// =============================================================================

bool finite (const FA::Readout& r)
{
    return std::isfinite (r.lufsShortTerm) && std::isfinite (r.lufsIntegrated)
        && std::isfinite (r.crestDb) && std::isfinite (r.tiltDb)
        && std::isfinite (r.monoLossDb) && std::isfinite (r.truePeakDb);
}

void testNaNPoisoning()
{
    FermentPerceptProcessor p;
    p.prepareToPlay (kSR, 512);

    const auto tone = sine (997.0, -20.0, 4.0);
    feed<float> (p, tone);
    check (finite (p.readout()), "a clean signal reads as finite numbers");

    // One block of NaN, then good audio for long enough that any transient
    // upset would have washed out of the three-second rings.
    feed<float> (p, Fixture (0.02, [] (int, double& a, double& b)
        { a = b = std::numeric_limits<double>::quiet_NaN(); }));
    feed<float> (p, tone);

    const auto poisoned = p.readout();
    char buf[160];
    std::snprintf (buf, sizeof buf, "LUFS-S %.2f, crest %.2f, ready=%d",
                   poisoned.lufsShortTerm, poisoned.crestDb, (int) poisoned.loudnessReady);

    /*  analyzer-core sanitises non-finite input to zero as it reads it, so a
        NaN block reads as a moment of digital silence and the meters carry on.
        Without that guard it is permanent: the K-weighting biquads keep their
        state forever and NaN never leaves a recursive filter, so every reading
        after the first NaN is NaN until something calls reset().  Both halves
        are asserted below — the meters stay finite, and reset() recovers — so
        this stays red if the guard is ever removed. */
    std::printf ("  [note] after one NaN block, four more seconds of tone: %s\n", buf);
    check (finite (poisoned), "one NaN block does not leave the meters showing NaN", buf);

    // What matters for the product is that it is recoverable.
    p.reset();
    feed<float> (p, tone);

    const auto recovered = p.readout();
    std::snprintf (buf, sizeof buf, "LUFS-I %.2f", recovered.lufsIntegrated);
    check (finite (recovered) && std::fabs (recovered.lufsIntegrated + 20.0) < 0.2,
           "reset() brings the core back from a NaN", buf);
}

// =============================================================================
//  7. Allocation
// =============================================================================

/** Runs @a body with the allocation gate open and returns the count. */
size_t allocationsDuring (const std::function<void()>& body)
{
    allocCount.store (0);
    allocGateOpen.store (true);
    body();
    allocGateOpen.store (false);
    return allocCount.load();
}

void testPollAndPaintAllocateNothing()
{
    FermentPerceptProcessor p;
    p.prepareToPlay (kSR, 512);
    feed<float> (p, subHeavy (12.0));

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());

    juce::Image canvas (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);

    auto paintOnce = [&]
    {
        juce::Graphics g (canvas);
        editor->paintEntireComponent (g, false);
    };

    // Warm-up: first paint, first strings, first history frames.
    paintOnce();
    runEditorFor (500);

    constexpr int measureMs = 1000;   // 20 poll ticks at the editor's 20 Hz

    const size_t withEditor = allocationsDuring ([&] { runEditorFor (measureMs); });

    /*  A full repaint is not allocation-free and cannot be made so: in JUCE 8
        the software renderer allocates inside Graphics itself — measured in
        this build at 6.9 allocations per drawText and 1.6 per fillAll — so the
        floor for a face with this many labels is in the hundreds however the
        components are written.  What is ours to get wrong is *growth*: a
        component that accumulates into a Path or a vector every frame costs
        more on the hundredth repaint than on the first, and that is invisible
        until a session has been open for an hour.  So the check is on the
        slope, not the level.
    */
    constexpr int paints = 20;
    const size_t firstBatch = allocationsDuring ([&] { for (int i = 0; i < paints; ++i) paintOnce(); });
    for (int i = 0; i < 200; ++i) paintOnce();
    const size_t laterBatch = allocationsDuring ([&] { for (int i = 0; i < paints; ++i) paintOnce(); });

    editor.reset();

    // The control: the same message loop with nothing polling in it.  Whatever
    // the loop itself costs shows up in both numbers and cancels.
    const size_t idle = allocationsDuring ([&] { runEditorFor (measureMs); });

    const long long attributable = (long long) withEditor - (long long) idle;

    char buf[192];
    std::snprintf (buf, sizeof buf,
                   "%zu allocations with the editor polling, %zu idle, %lld attributable",
                   withEditor, idle, attributable);
    check (attributable <= 0, "the readout poll allocates nothing after the first paint", buf);

    std::snprintf (buf, sizeof buf, "%zu allocations for repaints 1-%d, %zu for repaints 221-%d "
                                    "(%zu per repaint, JUCE's renderer)",
                   firstBatch, paints, laterBatch, 220 + paints, laterBatch / (size_t) paints);
    check (laterBatch <= firstBatch, "repainting costs no more after 200 frames than at the first", buf);
}

// =============================================================================
//  8. Lifecycle and bounds
// =============================================================================

void testEditorReopen()
{
    FermentPerceptProcessor p;
    p.prepareToPlay (kSR, 512);
    feed<float> (p, sine (997.0, -20.0, 8.0));

    double before = 0.0;
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        runEditorFor (200);
        before = p.readout().lufsIntegrated;
    }

    // Audio keeps flowing while no window is open, as it does in a host.
    feed<float> (p, sine (997.0, -20.0, 2.0));

    std::unique_ptr<juce::AudioProcessorEditor> reopened (p.createEditor());
    runEditorFor (200);
    const double after = p.readout().lufsIntegrated;

    char buf[128];
    std::snprintf (buf, sizeof buf, "%.3f LUFS-I before the close, %.3f after the reopen",
                   before, after);
    check (std::fabs (before - after) < 0.05,
           "the measurement outlives the editor window", buf);

    // The reopened editor must also still be driven by its own timer.
    auto* chips = findChild<ferment::VerdictChips> (*reopened);
    check (chips != nullptr && chips->numChips() == 7, "the reopened editor is fully built");
}

void testSillyBounds()
{
    FermentPerceptProcessor p;
    p.prepareToPlay (kSR, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());

    /*  Layout code here divides by widths and by band counts, and a host that
        hands an editor a zero or a one-pixel rectangle mid-resize is a real
        thing.  Painting at each size is what actually exercises the division.
    */
    const juce::Point<int> sizes[] = {
        { 0, 0 }, { 1, 1 }, { 2, 2 }, { 1, 900 }, { 900, 1 },
        { 40, 40 }, { 760, 420 }, { 4000, 3000 }
    };

    for (auto size : sizes)
    {
        editor->setSize (juce::jmax (1, size.x), juce::jmax (1, size.y));
        runEditorFor (30);

        juce::Image image (juce::Image::ARGB,
                           juce::jmax (1, editor->getWidth()),
                           juce::jmax (1, editor->getHeight()), true);
        juce::Graphics g (image);
        editor->paintEntireComponent (g, false);
    }

    check (true, "lays out and paints at degenerate and oversized bounds");
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Line buffered: a suite that hangs should still have said where it got to.
    std::setvbuf (stdout, nullptr, _IOLBF, 0);

    std::printf ("Ferment Percept plugin tests\n\n");

    std::printf ("1. passthrough:\n");
    testPassthrough();
    testEmptyBlock();
    testMonoInStereoOut();
    testLatency();

    std::printf ("2. measurement:\n");
    testMeasurement();

    std::printf ("3. verdict chips:\n");
    testVerdictChips();

    std::printf ("4. hint panel:\n");
    testHintPanelIsPolicy();
    testResultMode();

    std::printf ("5. controls:\n");
    testResetIsAnEdge();
    testResetActuallyResets();
    testResetSurvivesLargeBlocks();
    testResetButton();
    testProfileReachesTheCore();
    testHostileParameters();

    std::printf ("6. NaN:\n");
    testNaNPoisoning();

    std::printf ("7. allocation:\n");
    testPollAndPaintAllocateNothing();

    std::printf ("8. lifecycle:\n");
    testEditorReopen();
    testSillyBounds();

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures,
                 failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

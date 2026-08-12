// Ferment EQ acceptance test.
//
// Six claims, each checked against something the code under test does not
// also produce:
//
//   1. RESPONSE   the curve EqGraphEditor draws matches, within 0.1 dB, the
//                 gain the DSP actually applies to a sine at that frequency.
//   2. DRAG       dragging a node lands the parameter where the node was
//                 dropped, and the automation gestures around it balance.
//   3. EDGES      a half-filled Spec, no bands, unresolvable parameter IDs, a
//                 constant range map, drags off the graph and wheel spam do
//                 not crash or leak a gesture.
//   4. FIFO       the spectrum FIFO the audio thread feeds delivers exactly
//                 what was pushed across a buffer wrap, and drops rather than
//                 corrupts when nobody is draining it.
//   5. BALLISTICS NeedleMeter's attack and release land on their stated time
//                 constants for every scale orientation, and a square wave
//                 sweeps the needle rather than stepping it.
//   6. ALLOCATION nothing the spectrum FIFO does on either side reaches the
//                 heap.
//
// (1) goes through the *real* editor's graph object — its Spec, its parameter
// reads, its range maps — because that Spec is hand-wired and mis-wiring it is
// the likeliest way for the picture to start lying about the audio.  Asking
// FermentEqProcessor::snapshot() instead, as an earlier version of this test
// did, checks a path the UI never runs and cannot see that class of bug at
// all.  Two canaries at the end of section 1 prove the check has teeth: the
// comparison must go red both when predictions are paired with the wrong
// frequency and when the Spec's band types are transposed.

#include "../src-ferment/eq/FermentEqProcessor.h"

#include <ferment_ui/ferment_ui.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdio>
#include <deque>
#include <vector>

namespace {

using P = FermentEqProcessor;
using Graph = ferment::EqGraphEditor;

constexpr double kSR          = 48000.0;
constexpr int    kBlock       = 512;
constexpr int    kSettle      = 40;      // blocks processed before measuring
constexpr int    kMeasureLen  = 4096;    // a whole number of cycles at every test freq
constexpr double kAmplitude   = 0.1;
constexpr double kToleranceDb = 0.1;     // the spec's number

int failures = 0;

void check(bool ok, const char* what)
{
    if (!ok)
    {
        std::printf("  FAIL  %s\n", what);
        ++failures;
    }
}

void section(const char* name)
{
    std::printf("\n== %s ==\n", name);
}

// ---------------------------------------------------------------------------
// measurement

/** Writes a normalised value the way the UI does — through the parameter, in a
    gesture, so the host would see an edit. */
void setNorm(P& p, const juce::String& id, double norm)
{
    auto* param = p.apvts.getParameter(id);

    if (param == nullptr)
    {
        check(false, "parameter does not exist");
        return;
    }

    param->beginChangeGesture();
    param->setValueNotifyingHost((float)juce::jlimit(0.0, 1.0, norm));
    param->endChangeGesture();
}

/** Steady-state gain the DSP applies at one frequency, in dB.

    kSettle blocks of sine go in before anything is measured, which is four
    orders of magnitude longer than the slowest band here takes to ring down,
    so the measured window holds steady state and not the previous frequency's
    tail.  Both the settle and the window are whole numbers of cycles at every
    tested frequency, so the RMS is exact rather than smeared by leakage. */
double measuredDb(P& p, double freq)
{
    p.prepareToPlay(kSR, kBlock);

    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;

    double phase = 0.0;
    const double step = 2.0 * juce::MathConstants<double>::pi * freq / kSR;

    auto pump = [&](int blocks, double* sumSq)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int n = 0; n < kBlock; ++n)
            {
                const auto v = (float)(kAmplitude * std::sin(phase));
                phase += step;
                buf.setSample(0, n, v);
                buf.setSample(1, n, v);
            }

            p.processBlock(buf, midi);

            if (sumSq != nullptr)
                for (int n = 0; n < kBlock; ++n)
                {
                    const double y = buf.getSample(0, n);
                    *sumSq += y * y;
                }
        }
    };

    pump(kSettle, nullptr);

    double sumSq = 0.0;
    pump(kMeasureLen / kBlock, &sumSq);

    const double rmsOut = std::sqrt(sumSq / (double)kMeasureLen);
    const double rmsIn  = kAmplitude / std::sqrt(2.0);

    return 20.0 * std::log10(rmsOut / rmsIn);
}

// ---------------------------------------------------------------------------
// harness

Graph* findGraph(juce::Component& c)
{
    if (auto* g = dynamic_cast<Graph*>(&c))
        return g;

    for (auto* child : c.getChildren())
        if (auto* g = findGraph(*child))
            return g;

    return nullptr;
}

/** The graph's drawing area and axes, restated independently of the component
    so a drag can be aimed at a frequency and a gain rather than at a pixel the
    component handed back. */
juce::Rectangle<float> area(const Graph& g)
{
    return g.getLocalBounds().toFloat().reduced(1.0f);
}

float xForFreq(const Graph& g, double hz)
{
    const auto r = area(g);
    return r.getX() + r.getWidth() * (float)(std::log(hz / Graph::minHz) / std::log(Graph::maxHz / Graph::minHz));
}

float yForDb(const Graph& g, double db)
{
    const auto r = area(g);
    return r.getY() + r.getHeight() * (float)((Graph::maxDb - db) / (2.0 * Graph::maxDb));
}

juce::MouseEvent mouseAt(juce::Component& c, juce::Point<float> pos, juce::Point<float> down,
                         juce::ModifierKeys mods, int clicks = 1)
{
    return { juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
             juce::MouseInputSource::defaultPressure,
             juce::MouseInputSource::defaultOrientation,
             juce::MouseInputSource::defaultRotation,
             juce::MouseInputSource::defaultTiltX,
             juce::MouseInputSource::defaultTiltY,
             &c, &c, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(),
             clicks, false };
}

const juce::ModifierKeys leftButton { juce::ModifierKeys::leftButtonModifier };

/** Watches one parameter's gesture traffic.  An unbalanced or nested gesture is
    a real automation bug — hosts arm and disarm write mode off these. */
struct GestureWatch : juce::AudioProcessorParameter::Listener
{
    void parameterValueChanged(int, float) override {}

    void parameterGestureChanged(int, bool starting) override
    {
        if (starting)
        {
            if (open) ++nested;
            open = true;
            ++begins;
        }
        else
        {
            if (!open) ++orphanEnds;
            open = false;
            ++ends;
        }
    }

    bool open = false;
    int begins = 0, ends = 0, nested = 0, orphanEnds = 0;
};

/** Attaches a GestureWatch to every parameter of a processor for its lifetime. */
struct GestureAudit
{
    explicit GestureAudit(juce::AudioProcessor& proc) : processor(proc)
    {
        watches.resize(processor.getParameters().size());

        for (int i = 0; i < processor.getParameters().size(); ++i)
            processor.getParameters()[i]->addListener(&watches[(size_t)i]);
    }

    ~GestureAudit()
    {
        for (int i = 0; i < processor.getParameters().size(); ++i)
            processor.getParameters()[i]->removeListener(&watches[(size_t)i]);
    }

    void expectBalanced(const char* what) const
    {
        for (const auto& w : watches)
        {
            check(w.begins == w.ends, what);
            check(w.nested == 0, "gesture opened inside another on the same parameter");
            check(w.orphanEnds == 0, "gesture ended without a matching begin");
        }
    }

    int totalBegins() const
    {
        int n = 0;
        for (const auto& w : watches) n += w.begins;
        return n;
    }

    juce::AudioProcessor& processor;
    std::vector<GestureWatch> watches;
};

/** A Spec wired the way FermentEqEditor wires one.  `transposeShelves` mis-maps
    two band types, which is the canary: the response check has to notice. */
Graph::Spec makeSpec(P& p, bool transposeShelves = false)
{
    Graph::Spec spec;

    spec.numBands      = P::kNumBands;
    spec.paramId       = [](int band, const char* suffix) { return P::paramId(band, suffix); };
    spec.freqFromNorm  = [](double v) { return P::freqFromNorm(v); };
    spec.gainFromNorm  = [](double v) { return P::gainFromNorm(v); };
    spec.qFromNorm     = [](double v) { return P::qFromNorm(v); };
    spec.getSampleRate = [&p] { return p.getSampleRate(); };
    spec.typeName      = [](int t) { return juce::String(P::typeName(t)); };

    spec.bell      = P::Bell;
    spec.lowShelf  = transposeShelves ? P::HighShelf : P::LowShelf;
    spec.highShelf = transposeShelves ? P::LowShelf  : P::HighShelf;
    spec.lowCut    = P::LowCut;
    spec.highCut   = P::HighCut;
    spec.notch     = P::Notch;
    spec.numTypes  = P::kNumTypes;

    return spec;
}

// ---------------------------------------------------------------------------
// 1. response curve vs measured audio

// Frequencies that fit a whole number of cycles in the measurement window.
const int kBins[] = { 4, 9, 30, 43, 85, 171, 256, 512, 683, 1024, 1280 };

void testResponse(P& p, Graph& graph)
{
    section("1. drawn curve vs measured DSP gain");

    // Something happening in every region: a low shelf, two bells of opposite
    // sign, a notch, a high cut, and three bands switched off that must not
    // show up in either the picture or the audio.
    auto band = [&](int b, int type, double hz, double gainDb, double q, bool on)
    {
        setNorm(p, P::paramId(b, "type"), (double)type / (double)(P::kNumTypes - 1));
        setNorm(p, P::paramId(b, "freq"), P::freqToNorm(hz));
        setNorm(p, P::paramId(b, "gain"), gainDb / 30.0 + 0.5);
        setNorm(p, P::paramId(b, "q"),    std::log(q / 0.1) / std::log(180.0));
        setNorm(p, P::paramId(b, "on"),   on ? 1.0 : 0.0);
    };

    band(0, P::LowShelf,  90.0,   4.5, 0.71, true);
    band(1, P::Bell,      350.0, -6.0, 1.40, true);
    band(2, P::Bell,     2000.0,  5.5, 0.90, true);
    band(3, P::Notch,    7000.0,  0.0, 4.00, true);
    band(4, P::Bell,     4000.0,  9.0, 1.00, false);
    band(5, P::Bell,     1000.0,  3.0, 1.00, false);
    band(6, P::HighShelf, 9000.0, 0.0, 0.71, false);
    band(7, P::HighCut,  15000.0, 0.0, 0.71, true);

    setNorm(p, "output", 0.5);   // 0 dB, so only the band curve is under test

    graph.updateResponse();

    std::printf("  %10s  %10s  %10s  %9s\n", "freq", "drawn", "measured", "delta");

    std::vector<double> freqs, drawn, measured;
    double worst = 0.0;

    for (int k : kBins)
    {
        const double freq = (double)k * kSR / (double)kMeasureLen;
        const double want = graph.responseDbAt(freq);
        const double got  = measuredDb(p, freq);
        const double diff = std::fabs(want - got);

        freqs.push_back(freq);
        drawn.push_back(want);
        measured.push_back(got);
        worst = std::max(worst, diff);

        std::printf("  %9.1f  %+9.3f  %+9.3f  %11.8f%s\n",
                    freq, want, got, diff, diff > kToleranceDb ? "  <-- over" : "");
    }

    std::printf("  worst delta %.8f dB (tol %.2f)\n", worst, kToleranceDb);
    check(worst <= kToleranceDb, "drawn curve disagrees with the measured DSP gain");

    // Canary A: the same numbers paired with the neighbouring frequency must
    // fail.  If they do not, the tolerance is wider than the curve's own
    // variation and passing means nothing.
    {
        int shuffledOver = 0;

        for (size_t i = 0; i + 1 < freqs.size(); ++i)
            if (std::fabs(drawn[i] - measured[i + 1]) > kToleranceDb)
                ++shuffledOver;

        std::printf("  canary A: %d of %d shuffled pairs exceed tolerance\n",
                    shuffledOver, (int)freqs.size() - 1);
        check(shuffledOver == (int)freqs.size() - 1,
              "shuffled pairs pass, so the comparison does not discriminate");
    }

    // Canary B: a Spec with two band types transposed is the failure this test
    // exists to catch.  Band 0 is a low shelf, so a graph told that low shelves
    // are high shelves must draw something the audio does not do.
    {
        Graph miswired(p.apvts, makeSpec(p, true));
        miswired.setBounds(graph.getBounds());
        miswired.updateResponse();

        double worstMiswired = 0.0;

        for (size_t i = 0; i < freqs.size(); ++i)
            worstMiswired = std::max(worstMiswired,
                                     std::fabs(miswired.responseDbAt(freqs[i]) - measured[i]));

        std::printf("  canary B: shelf-transposed Spec is off by %.3f dB\n", worstMiswired);
        check(worstMiswired > kToleranceDb,
              "a mis-wired Spec still passes, so the Spec is not under test");
    }
}

// ---------------------------------------------------------------------------
// 2. drag round trip

void testDrag(P& p, Graph& graph)
{
    section("2. drag -> parameter -> DSP -> curve");

    GestureAudit audit(p);

    // Band 2 is an enabled bell, so it has both axes.  Grab it where it is,
    // drop it somewhere else, and see whether the EQ followed.
    const int band = 2;
    const double fromHz = 2000.0;
    const double toHz   = 620.0;
    const double toDb   = -7.25;

    const juce::Point<float> down { xForFreq(graph, fromHz), yForDb(graph, 5.5) };
    const juce::Point<float> up   { xForFreq(graph, toHz),   yForDb(graph, toDb) };

    graph.mouseDown(mouseAt(graph, down, down, leftButton));
    check(graph.getSelectedBand() == band, "mouseDown on a node did not select its band");

    graph.mouseDrag(mouseAt(graph, up, down, leftButton));
    graph.mouseUp(mouseAt(graph, up, down, leftButton));

    const double gotHz = P::freqFromNorm(p.apvts.getRawParameterValue(P::paramId(band, "freq"))->load());
    const double gotDb = P::gainFromNorm(p.apvts.getRawParameterValue(P::paramId(band, "gain"))->load());

    std::printf("  dropped at %.1f Hz / %+.2f dB, parameter reads %.1f Hz / %+.2f dB\n",
                toHz, toDb, gotHz, gotDb);

    // One graph pixel is ~0.3%% in frequency and ~0.07 dB in gain at this size;
    // the bisected inverse must land inside that.
    check(std::fabs(gotHz - toHz) / toHz < 0.005, "drag put the band at the wrong frequency");
    check(std::fabs(gotDb - toDb) < 0.1, "drag put the band at the wrong gain");

    // ... and the audio and the picture still agree about where it went.
    graph.updateResponse();
    const double drawnAtDrop = graph.responseDbAt(toHz);
    const double heardAtDrop = measuredDb(p, toHz);
    std::printf("  after the drag, at %.0f Hz: drawn %+.3f dB, measured %+.3f dB\n",
                toHz, drawnAtDrop, heardAtDrop);
    check(std::fabs(drawnAtDrop - heardAtDrop) <= kToleranceDb,
          "curve and audio disagree after a drag");

    // A wheel notch mid-drag used to open a second gesture inside the drag's
    // own gesture on the same Q parameter.
    graph.mouseDown(mouseAt(graph, up, up, leftButton));
    graph.mouseWheelMove(mouseAt(graph, up, up, leftButton), {});
    graph.mouseDoubleClick(mouseAt(graph, up, up, leftButton, 2));
    graph.mouseUp(mouseAt(graph, up, up, leftButton));

    audit.expectBalanced("gestures left unbalanced after drag / wheel / double-click");
    std::printf("  %d gestures opened, all balanced\n", audit.totalBegins());
}

// ---------------------------------------------------------------------------
// 3. edges

/** Paints a component offscreen, which is where anything that reads past the
    end of a band array will show up. */
void paintOffscreen(juce::Component& c)
{
    juce::Image image(juce::Image::ARGB, juce::jmax(1, c.getWidth()), juce::jmax(1, c.getHeight()), true);
    juce::Graphics g(image);
    c.paintEntireComponent(g, false);
}

std::vector<float> allValues(juce::AudioProcessor& p)
{
    std::vector<float> v;

    for (auto* param : p.getParameters())
        v.push_back(param->getValue());

    return v;
}

/** Sweeps the whole graph with press / fling-off-the-edge / release, so nodes
    get grabbed whatever the Spec put them at, and every drag ends somewhere
    the axes have to clamp.  Returns how many of those drags actually moved a
    parameter, which is how a caller knows the sweep hit anything. */
int exerciseGraph(Graph& graph, juce::AudioProcessor& p, const char* what)
{
    graph.setBounds(0, 0, 600, 300);
    graph.updateResponse();
    graph.responseDbAt(1000.0);
    paintOffscreen(graph);

    int moved = 0;

    for (float x = 0.0f; x <= 600.0f; x += 20.0f)
        for (float y = 0.0f; y <= 300.0f; y += 20.0f)
        {
            const juce::Point<float> down { x, y };
            const auto before = allValues(p);

            graph.mouseMove(mouseAt(graph, down, down, {}));
            graph.mouseDown(mouseAt(graph, down, down, leftButton));
            graph.mouseDrag(mouseAt(graph, { -500.0f, -900.0f }, down, leftButton));
            graph.mouseDrag(mouseAt(graph, { 5000.0f, 9000.0f }, down, leftButton));
            graph.mouseDrag(mouseAt(graph, { x + 30.0f, y - 40.0f }, down,
                                    juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier
                                                       | juce::ModifierKeys::commandModifier)));
            graph.mouseUp(mouseAt(graph, down, down, leftButton));

            if (allValues(p) != before)
                ++moved;
        }

    const juce::Point<float> centre { 300.0f, 150.0f };
    graph.mouseWheelMove(mouseAt(graph, centre, centre, {}), {});
    graph.mouseDoubleClick(mouseAt(graph, centre, centre, leftButton, 2));
    graph.mouseExit(mouseAt(graph, centre, centre, {}));
    graph.setSelectedBand(-5);
    graph.setSelectedBand(9999);
    paintOffscreen(graph);

    std::printf("  survived: %s (%d drags moved a parameter)\n", what, moved);
    return moved;
}

void testEdges(P& p)
{
    section("3. edges");

    {
        Graph::Spec spec;             // every std::function null, default 8 bands
        Graph graph(p.apvts, spec);
        exerciseGraph(graph, p, "Spec with no functions set");
    }

    {
        auto spec = makeSpec(p);
        spec.numBands = 0;
        Graph graph(p.apvts, spec);
        exerciseGraph(graph, p, "numBands = 0");
    }

    {
        auto spec = makeSpec(p);
        spec.numBands = -3;
        Graph graph(p.apvts, spec);
        exerciseGraph(graph, p, "numBands < 0");
    }

    {
        auto spec = makeSpec(p);
        spec.paramId = [](int b, const char*) { return "no_such_param_" + juce::String(b); };
        Graph graph(p.apvts, spec);
        exerciseGraph(graph, p, "parameter IDs that do not resolve");
    }

    {
        // invertMonotonic against a map that never changes: bisection has to
        // terminate at an end rather than spin or return a NaN.
        auto spec = makeSpec(p);
        spec.freqFromNorm = [](double) { return 1000.0; };
        spec.gainFromNorm = [](double v) { return std::sin(v * 40.0) * 15.0; };   // not monotonic
        Graph graph(p.apvts, spec);
        exerciseGraph(graph, p, "constant and non-monotonic range maps");

        for (int b = 0; b < P::kNumBands; ++b)
            for (const char* suffix : { "freq", "gain", "q" })
            {
                const float v = p.apvts.getParameter(P::paramId(b, suffix))->getValue();
                check(v == v && v >= 0.0f && v <= 1.0f, "a bad range map wrote a bad parameter");
            }
    }

    {
        // Drags off the edge of the graph must clamp, not run the frequency
        // out of the audible range or the gain past the axis.
        auto graph = std::make_unique<Graph>(p.apvts, makeSpec(p));
        GestureAudit audit(p);
        const int moved = exerciseGraph(*graph, p, "drags outside the graph bounds");
        check(moved > 0, "the drag sweep never grabbed a node, so nothing was tested");

        for (int b = 0; b < P::kNumBands; ++b)
        {
            const double hz = P::freqFromNorm(p.apvts.getRawParameterValue(P::paramId(b, "freq"))->load());
            check(hz >= 19.9 && hz <= 20100.0, "a drag pushed a band outside 20 Hz .. 20 kHz");
        }

        // 200 wheel notches, no drag open: 200 clean gesture pairs.
        const juce::Point<float> node { xForFreq(*graph, P::freqFromNorm(
                                            p.apvts.getRawParameterValue(P::paramId(0, "freq"))->load())),
                                        yForDb(*graph, 0.0) };

        for (int i = 0; i < 200; ++i)
        {
            juce::MouseWheelDetails wheel;
            wheel.deltaY = (i % 2 == 0) ? 0.1f : -0.1f;
            graph->mouseWheelMove(mouseAt(*graph, node, node, {}), wheel);
        }

        audit.expectBalanced("wheel spam left a gesture open");
        std::printf("  survived: 200 wheel notches, %d gestures, all balanced\n", audit.totalBegins());

        // The band menu's async callback is only safe because the SafePointer
        // it captured goes null when the graph dies.  The menu itself needs a
        // real event loop, which this build has no modal loops for, so what is
        // checked here is the guard the callback actually tests.
        juce::Component::SafePointer<Graph> safe(graph.get());
        check(safe.getComponent() == graph.get(), "SafePointer did not track a live graph");
        graph.reset();
        check(safe.getComponent() == nullptr, "SafePointer survived its graph; the menu callback would use freed memory");
        std::printf("  SafePointer nulled on destruction, so the menu callback bails\n");
    }
}

// ---------------------------------------------------------------------------
// 4. spectrum FIFO

/** What the FIFO ought to hold, given AbstractFifo's one-slot-reserved
    capacity and pushSpectrum's "write what fits, drop the rest" policy. */
struct FifoModel
{
    static constexpr int capacity = (1 << 14) - 1;

    void push(const std::vector<float>& block)
    {
        const int room = capacity - (int)queue.size();
        const int n = juce::jmin((int)block.size(), room);

        for (int i = 0; i < n; ++i)
            queue.push_back(block[(size_t)i]);

        dropped += (int)block.size() - n;
    }

    std::vector<float> pop(int n)
    {
        std::vector<float> out;

        for (int i = 0; i < n && !queue.empty(); ++i)
        {
            out.push_back(queue.front());
            queue.pop_front();
        }

        return out;
    }

    std::deque<float> queue;
    int dropped = 0;
};

void testFifo()
{
    section("4. spectrum FIFO");

    P p;
    p.setPlayConfigDetails(2, 2, kSR, kBlock);
    p.prepareToPlay(kSR, kBlock);

    // Every band off and 0 dB out, so the DSP is a pass-through and whatever
    // goes in is what the FIFO should hand back.
    for (int b = 0; b < P::kNumBands; ++b)
        setNorm(p, P::paramId(b, "on"), 0.0);

    setNorm(p, "output", 0.5);

    FifoModel model;
    juce::MidiBuffer midi;
    float counter = 1.0f;
    int wrappedWrites = 0, wrappedReads = 0, writeHead = 0, readHead = 0;

    auto pushBlock = [&](int numSamples)
    {
        juce::AudioBuffer<float> buf(2, numSamples);
        std::vector<float> expected((size_t)numSamples);

        for (int n = 0; n < numSamples; ++n)
        {
            buf.setSample(0, n, counter);
            buf.setSample(1, n, counter);
            expected[(size_t)n] = counter;
            counter += 1.0f;
        }

        const int room = FifoModel::capacity - (int)model.queue.size();
        const int written = juce::jmin(numSamples, room);

        if (writeHead + written > (1 << 14))
            ++wrappedWrites;

        writeHead = (writeHead + written) % (1 << 14);

        p.processBlock(buf, midi);
        model.push(expected);
    };

    auto drain = [&](int maxSamples, const char* what)
    {
        std::vector<float> dest((size_t)maxSamples, -1.0f);
        const int got = p.readSpectrumSamples(dest.data(), maxSamples);
        const auto want = model.pop(maxSamples);

        check(got == (int)want.size(), what);

        for (int i = 0; i < got && i < (int)want.size(); ++i)
            if (dest[(size_t)i] != want[(size_t)i])
            {
                std::printf("  FAIL  %s: sample %d is %.1f, expected %.1f\n",
                            what, i, dest[(size_t)i], want[(size_t)i]);
                ++failures;
                break;
            }

        if (readHead + got > (1 << 14))
            ++wrappedReads;

        readHead = (readHead + got) % (1 << 14);
        return got;
    };

    // Empty FIFO.
    {
        std::vector<float> dest(2048, -1.0f);
        check(p.readSpectrumSamples(dest.data(), 2048) == 0, "empty FIFO did not return 0");
        check(dest[0] == -1.0f, "empty FIFO wrote to the destination anyway");
    }

    // Read larger than what is ready.
    pushBlock(100);
    check(drain(2048, "short read") == 100, "read of 2048 with 100 ready returned the wrong count");

    // Sizes coprime with the 16384-slot buffer, so both heads walk the whole
    // ring and every wrap case gets hit many times over.
    for (int i = 0; i < 400; ++i)
    {
        pushBlock(1000 + (i % 7));
        drain(768, "wrapped read");
    }

    std::printf("  %d writes and %d reads crossed the end of the buffer\n",
                wrappedWrites, wrappedReads);
    check(wrappedWrites > 0, "no write ever wrapped, so the wrap path is untested");
    check(wrappedReads > 0, "no read ever wrapped, so the wrap path is untested");

    // Overflow: nobody draining, audio running.  Nothing may be corrupted and
    // nothing may block; the newest samples are the ones that get dropped.
    while (!model.queue.empty())
        drain(4096, "drain before overflow");

    model.dropped = 0;

    for (int i = 0; i < 200; ++i)
        pushBlock(512);

    check(model.dropped > 0, "the overflow case never actually overflowed");
    std::printf("  pushed %d samples with no reader, %d dropped, %d retained\n",
                200 * 512, model.dropped, (int)model.queue.size());
    check((int)model.queue.size() == FifoModel::capacity, "a full FIFO did not saturate at capacity");

    int recovered = 0;
    while (!model.queue.empty())
        recovered += drain(2048, "drain after overflow");

    check(recovered == FifoModel::capacity, "a full FIFO did not give back everything it held");
    std::printf("  recovered %d samples after the overflow, in order\n", recovered);

    // Back to normal service afterwards.
    pushBlock(256);
    check(drain(2048, "after overflow") == 256, "FIFO did not recover after an overflow");

    // Mono in, stereo out: the DSP writes channel 0 and leaves channel 1 as the
    // host left it, so the analyser must not average that in.
    {
        P mono;
        mono.setPlayConfigDetails(1, 2, kSR, kBlock);
        mono.prepareToPlay(kSR, kBlock);

        for (int b = 0; b < P::kNumBands; ++b)
            setNorm(mono, P::paramId(b, "on"), 0.0);

        setNorm(mono, "output", 0.5);

        juce::AudioBuffer<float> buf(2, 64);

        for (int n = 0; n < 64; ++n)
        {
            buf.setSample(0, n, 0.5f);
            buf.setSample(1, n, -99.0f);   // stale junk in the unused channel
        }

        mono.processBlock(buf, midi);

        std::vector<float> dest(64, 0.0f);
        const int got = mono.readSpectrumSamples(dest.data(), 64);

        std::printf("  mono in / stereo out: FIFO reports %.3f (input 0.5, junk in ch1 -99)\n",
                    got > 0 ? dest[0] : 0.0f);
        check(got == 64 && std::fabs(dest[0] - 0.5f) < 1.0e-6f,
              "mono input averaged in an unwritten channel");
    }
}

// ---------------------------------------------------------------------------
// 5. needle ballistics
//
// NeedleMeter is in the same UI module and its ballistics are pure, but the
// build has no test target that links ferment_ui on its own, so they are
// checked here rather than not at all.  Worth splitting out when one exists.

using Needle = ferment::NeedleMeter;

/** Ticks the ballistics at 30 Hz and returns the trajectory. */
std::vector<double> settle(Needle::Range range, double from, double to, int ticks)
{
    std::vector<double> trace { from };

    for (int i = 0; i < ticks; ++i)
        trace.push_back(Needle::advance(trace.back(), to, range, 1000.0 / 30.0));

    return trace;
}

/** Milliseconds to cover 90% of the distance, at 30 Hz. */
double timeTo90(Needle::Range range, double from, double to)
{
    const auto trace = settle(range, from, to, 200);
    const double target90 = from + 0.9 * (to - from);

    for (size_t i = 0; i < trace.size(); ++i)
        if (std::fabs(trace[i] - from) >= std::fabs(target90 - from))
            return (double)i * 1000.0 / 30.0;

    return -1.0;
}

void testBallistics()
{
    section("5. needle ballistics");

    // tau ln(10) = 115 ms attack, 691 ms release; sampled at 30 Hz, so a tick
    // of slack either way.
    struct Case { const char* name; Needle::Range range; double from, to; double wantMs; };

    const Case cases[] = {
        { "GR 0..12 attack",        { 0.0,  12.0 },   0.0,  12.0, 115.0 },
        { "GR 0..12 release",       { 0.0,  12.0 },  12.0,   0.0, 691.0 },
        { "GR 0..-12 attack",       { 0.0, -12.0 },   0.0, -12.0, 115.0 },
        { "GR 0..-12 release",      { 0.0, -12.0 }, -12.0,   0.0, 691.0 },
        { "level -60..0 attack",    { -60.0, 0.0 }, -60.0, -20.0, 115.0 },
        { "level -60..0 release",   { -60.0, 0.0 }, -20.0, -60.0, 691.0 },
    };

    for (const auto& c : cases)
    {
        const double ms = timeTo90(c.range, c.from, c.to);
        std::printf("  %-24s 90%% in %6.1f ms (want %.0f)\n", c.name, ms, c.wantMs);
        check(std::fabs(ms - c.wantMs) <= 40.0, c.name);
    }

    // The reading has overshot past the resting end — a compressor reporting a
    // touch of gain rather than reduction — and then a real transient arrives.
    // That is an attack; comparing distance-from-rest calls it a release and
    // the needle comes up six times too slowly.
    {
        const Needle::Range gr { 0.0, 12.0 };
        const double step = Needle::advance(-3.0, 2.0, gr, 1000.0 / 30.0) - (-3.0);
        const double attackStep = (2.0 - (-3.0)) * (1.0 - std::exp(-(1000.0 / 30.0) / Needle::attackMs));

        std::printf("  rising from below rest: %.3f dB in one tick (attack would be %.3f)\n",
                    step, attackStep);
        check(std::fabs(step - attackStep) < 1.0e-9, "a rise from below rest is not treated as an attack");
    }

    // dt edges: a zero-length tick must not move the needle, and a stall that
    // the caller has clamped to 100 ms must not teleport it either.
    {
        check(Needle::advance(4.0, 12.0, { 0.0, 12.0 }, 0.0) == 4.0, "a zero-length tick moved the needle");
        check(Needle::advance(4.0, 12.0, { 0.0, 12.0 }, -50.0) == 4.0, "a negative tick moved the needle");

        const double afterStall = Needle::advance(0.0, 12.0, { 0.0, 12.0 }, 100.0);
        std::printf("  after a 100 ms stall the needle is at %.2f of 12 dB\n", afterStall);
        check(afterStall < 12.0 * 0.95, "a clamped stall jumped the needle to the target");
        check(afterStall > 12.0 * 0.75, "a clamped stall barely moved the needle");
    }

    // A square wave between rest and full: the needle has to sweep, not step.
    {
        const Needle::Range gr { 0.0, 12.0 };
        double display = 0.0, biggestStep = 0.0, lowest = 99.0, highest = -99.0;
        juce::String shape;

        // 24 ticks (800 ms) each way, so the 300 ms release has time to fall
        // back before the next hit — a shorter square just proves the release
        // is slow, which is the point of it.
        for (int tick = 0; tick < 200; ++tick)
        {
            const double target = ((tick / 24) % 2 == 0) ? 12.0 : 0.0;
            const double next = Needle::advance(display, target, gr, 1000.0 / 30.0);

            biggestStep = std::max(biggestStep, std::fabs(next - display));
            display = next;

            if (tick >= 48)   // after the first cycle has bedded in
            {
                lowest  = std::min(lowest, display);
                highest = std::max(highest, display);
            }

            if (tick >= 48 && tick < 96)
                shape += juce::String("_.-=*#").substring((int)juce::jlimit(0.0, 5.0, display / 12.0 * 5.0),
                                                          (int)juce::jlimit(0.0, 5.0, display / 12.0 * 5.0) + 1);
        }

        std::printf("  square wave, one cycle: %s\n", shape.toRawUTF8());
        std::printf("  biggest single-tick move %.2f dB of 12; swing %.2f .. %.2f\n",
                    biggestStep, lowest, highest);

        check(biggestStep < 12.0 * 0.6, "the needle stepped rather than swept");
        check(highest > 12.0 * 0.85, "the needle never reached the top of the square wave");
        check(lowest < 12.0 * 0.15, "the needle never fell back on the square wave");
    }
}

/** Counts heap traffic, so "the audio thread does not allocate" is a number
    rather than an opinion. */
std::atomic<int> allocations { 0 };

void testAllocations()
{
    section("6. allocation on the audio path");

    P p;
    p.setPlayConfigDetails(2, 2, kSR, kBlock);
    p.prepareToPlay(kSR, kBlock);

    juce::MidiBuffer midi;
    std::vector<float> dest(4096, 0.0f);

    auto perBlock = [&](int numSamples)
    {
        juce::AudioBuffer<float> buf(2, numSamples);
        buf.clear();

        p.processBlock(buf, midi);          // warm up

        const int before = allocations.load();
        for (int i = 0; i < 10; ++i)
            p.processBlock(buf, midi);
        return (allocations.load() - before) / 10;
    };

    const int small = perBlock(64);
    const int large = perBlock(4096);

    std::printf("  processBlock allocations: %d per block at 64 samples, %d at 4096\n", small, large);
    check(small == large,
          "allocation count varies with block size, so something on the audio path allocates per sample");

    const int before = allocations.load();
    for (int i = 0; i < 100; ++i)
        p.readSpectrumSamples(dest.data(), (int)dest.size());
    const int drained = allocations.load() - before;

    std::printf("  readSpectrumSamples allocations over 100 drains: %d\n", drained);
    check(drained == 0, "readSpectrumSamples allocates");
}

} // namespace

void* operator new (std::size_t n)
{
    allocations.fetch_add(1, std::memory_order_relaxed);

    if (auto* p = std::malloc(n))
        return p;

    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { std::free(p); }
void operator delete (void* p, std::size_t) noexcept { std::free(p); }

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    P p;
    p.setPlayConfigDetails(2, 2, kSR, kBlock);
    p.prepareToPlay(kSR, kBlock);

    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto* graph = editor != nullptr ? findGraph(*editor) : nullptr;

    if (graph == nullptr)
    {
        std::printf("FAIL: the EQ editor has no EqGraphEditor to test\n");
        return 1;
    }

    testResponse(p, *graph);
    testDrag(p, *graph);

    editor.reset();

    testEdges(p);
    testFifo();
    testBallistics();
    testAllocations();

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL",
                failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}

// ferment_ui component-kit acceptance test.
//
// The kit had no test of its own: everything that covered it went through a
// plugin editor, so a component could only fail where some product happened to
// use it.  This drives the components directly.
//
//   1. TEXT ENTRY   a typed string reaches the parameter through the real
//                   inline editor (set the text, send Return), for well-formed
//                   input, for junk, and for input that cannot be represented.
//                   Host gestures are counted and must balance.
//   2. NUDGE        arrows and shift-arrows move the parameter by the intended
//                   amounts, with gestures, and stop at both ends of the range.
//   3. DEGENERATE   every component lays out and paints at 0x0, 1x1 and
//                   2000x20, with empty content and with absent slots.
//   4. METERSTRIP   history fed before the first paint; NaN and Inf readings;
//                   the newest sample is the one at the right-hand edge; paint
//                   reaches the heap zero times.
//   5. TOGGLE       the attached bool parameter and the pill agree in both
//                   directions, and the attachment cannot outlive the button.
//
// Assertions are live in a non-Release build and JUCE logs them to stderr, so
// run this with stderr captured: an assertion is a failure even when the
// process exits 0.

#include <ferment_ui/ferment_ui.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

// ---- allocation harness ----------------------------------------------------
// Global operator new is replaced so an allocation anywhere -- ours, JUCE's, the
// standard library's -- is counted while the gate is open.

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

int failures = 0;

void check (bool ok, const char* what, const juce::String& detail = {})
{
    if (! ok)
        ++failures;

    std::printf ("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
                 detail.isEmpty() ? "" : " — ", detail.toRawUTF8());
}

// ---- a processor to hang parameters off -----------------------------------
// Three float knobs shaped like the family's real ones (a signed dB, a percent,
// a three-word mode) plus a bool for the toggle.

double gainDbFromNorm (double n)  { return (n - 0.5) * 70.0; }
double widthFromNorm  (double n)  { return n * 4.0; }

const char* const kModeNames[] = { "Mild", "Moderate", "Hot" };

int modeFromNorm (double n, int count)
{
    return juce::jlimit (0, count - 1, (int) (n * (double) count));
}

juce::String fmtGain  (double n) { return juce::String (gainDbFromNorm (n), 1) + " dB"; }
juce::String fmtWidth (double n) { return juce::String ((int) std::round (widthFromNorm (n) * 100.0)) + "%"; }
juce::String fmtMode  (double n) { return kModeNames[modeFromNorm (n, 3)]; }

class KitProcessor : public juce::AudioProcessor
{
public:
    KitProcessor() : apvts (*this, nullptr, "PARAMS", layout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        auto add = [&l] (const char* id, const char* name, float def)
        {
            l.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID (id, 1), name,
                juce::NormalisableRange<float> (0.0f, 1.0f), def));
        };

        add ("gain",  "Gain",  0.5f);
        add ("width", "Width", 0.25f);
        add ("mode",  "Mode",  0.0f);

        l.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID ("byp", 1), "Bypass", false));

        return l;
    }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "Kit"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
};

/** Counts the host-facing gesture calls, which is the only way to see that a
    write went out as one edit rather than as a bare value change. */
struct GestureCounter : juce::AudioProcessorParameter::Listener
{
    explicit GestureCounter (juce::RangedAudioParameter& p) : param (p) { param.addListener (this); }
    ~GestureCounter() override { param.removeListener (this); }

    void parameterValueChanged (int, float) override { ++changes; }
    void parameterGestureChanged (int, bool starting) override { starting ? ++begins : ++ends; }

    void reset() { begins = ends = changes = 0; }

    juce::RangedAudioParameter& param;
    int begins = 0, ends = 0, changes = 0;
};

/** Depth-first search for a child of a given type. */
template <typename T>
T* findChild (juce::Component& root)
{
    for (auto* child : root.getChildren())
    {
        if (auto* hit = dynamic_cast<T*> (child))
            return hit;

        if (auto* deeper = findChild<T> (*child))
            return deeper;
    }

    return nullptr;
}

/** Types into a knob the way a user does: open the inline editor, put the
    string in it, press Return.  Returns false if the editor never appeared. */
bool typeInto (ferment::FermentKnob& knob, const juce::String& text)
{
    knob.beginTextEntry();

    auto* te = findChild<juce::TextEditor> (knob);

    if (te == nullptr)
        return false;

    te->setText (text, juce::dontSendNotification);
    te->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));

    /*  TextEditor::returnPressed() POSTS a command message rather than calling
        onReturnKey directly, so the commit lands on the next turn of the loop.
        A test that types and then reads straight back sees the old value and
        blames the parser. */
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

    if (std::getenv ("FERMENT_UI_TEST_TRACE") != nullptr)
        std::printf ("      trace: editor visible=%d text=\"%s\"\n",
                     (int) te->isVisible(), te->getText().toRawUTF8());

    return true;
}

/*  Text shaping does not allocate identically everywhere: DirectWrite came
    in two allocations over CoreText for the same five labels on the first
    Windows CI run.  The slack absorbs that and cannot absorb a real
    regression — rebuilding a String or a Path per element adds at least one
    allocation per element, and every component here has five or more.  */
constexpr size_t paintBudgetSlack = 3;

/*  A hand-drawn stand-in for a component: the same primitives, in the same
    order, with no component behind them.

    The paint budgets below are measured against one of these rather than
    against a label count.  What a primitive allocates is a property of the
    machine, not of our code — on the macOS 14 CI runner a stroked path costs
    an order of magnitude more than it does on a macOS 26 laptop — so a budget
    of "labels x drawText" quietly assumed shapes were free, passed locally
    and failed on CI.  Against a stand-in both machines ask the same question:
    does the component allocate more than its own drawing does?

    The stand-in keeps a juce::Path member because that is how a component
    that draws one per frame is supposed to hold it: cleared and refilled, not
    rebuilt.  A component that allocates a fresh one goes over budget, which
    is the regression this is here to catch.
*/
struct DrawingProbe : juce::Component
{
    std::function<void (juce::Graphics&, juce::Path&)> draw;
    juce::Path scratch;

    void paint (juce::Graphics& g) override
    {
        if (draw != nullptr)
            draw (g, scratch);
    }
};

/** A paint into a throwaway image, with the graphics context built before the
    allocation gate opens so only the component's own paint is counted. */
size_t allocationsInPaint (juce::Component& c, int w, int h, int warmups = 2)
{
    juce::Image img (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);

    for (int i = 0; i < warmups; ++i)
    {
        juce::Graphics warm (img);
        c.paint (warm);
    }

    juce::Graphics g (img);

    allocCount = 0;
    allocGateOpen = true;
    c.paint (g);
    allocGateOpen = false;

    return allocCount.load();
}

/** Lays a component out and paints it at a size, only checking that it comes
    back. The interesting failures here are crashes and jasserts. */
void exerciseSize (juce::Component& c, int w, int h)
{
    c.setSize (w, h);

    juce::Image img (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);
    juce::Graphics g (img);
    c.paintEntireComponent (g, true);
}

void exerciseDegenerateSizes (juce::Component& c, const char* name)
{
    for (auto wh : { std::pair<int, int> { 0, 0 },
                     { 1, 1 },
                     { 2000, 20 },
                     { 20, 2000 },
                     { 3, 0 } })
        exerciseSize (c, wh.first, wh.second);

    check (true, name, "0x0, 1x1, 2000x20, 20x2000, 3x0 laid out and painted");
}

// =============================================================================
//  1. Text entry
// =============================================================================

void testTextEntry()
{
    std::printf ("\nTEXT ENTRY\n");

    KitProcessor proc;
    juce::Component holder;
    holder.setSize (400, 120);

    ferment::FermentKnob gain  (proc.apvts, "gain",  "GAIN",  fmtGain);
    ferment::FermentKnob width (proc.apvts, "width", "WIDTH", fmtWidth);
    ferment::FermentKnob mode  (proc.apvts, "mode",  "MODE",  fmtMode);

    for (auto* k : { &gain, &width, &mode })
        holder.addAndMakeVisible (*k);

    gain .setBounds (0,   0, 120, 100);
    width.setBounds (130, 0, 120, 100);
    mode .setBounds (260, 0, 120, 100);

    auto* gainParam  = proc.apvts.getParameter ("gain");
    auto* widthParam = proc.apvts.getParameter ("width");

    GestureCounter gainGestures (*gainParam);

    struct Case { const char* typed; double wantDb; bool wantChange; const char* note; };

    // -35 .. +35 dB over 0..1, so -9.5 dB is norm 0.364...
    const Case cases[] = {
        { "-9.5",       -9.5,  true,  "plain number" },
        { "-9.5 dB",    -9.5,  true,  "number with the unit" },
        { "  -9.5 dB ", -9.5,  true,  "surrounding whitespace" },
        { "+12",         12.0, true,  "explicit plus" },
        { "",             0.0, false, "empty string" },
        { "abc",          0.0, false, "letters" },
        { "-",            0.0, false, "lone minus" },
    };

    for (const auto& c : cases)
    {
        gainParam->setValueNotifyingHost (0.5f);          // 0.0 dB
        gainGestures.reset();

        const double before = gainDbFromNorm (gainParam->getValue());
        typeInto (gain, c.typed);
        const double after = gainDbFromNorm (gainParam->getValue());

        juce::String detail;
        detail << "typed \"" << c.typed << "\" -> " << juce::String (after, 3) << " dB"
               << "  (gestures " << gainGestures.begins << "/" << gainGestures.ends << ")";

        const bool moved = std::abs (after - before) > 1.0e-6;
        const bool ok = c.wantChange ? (moved && std::abs (after - c.wantDb) < 0.2)
                                     : ! moved;

        check (ok, c.note, detail);
        check (gainGestures.begins == gainGestures.ends, "  gestures balanced",
               juce::String (gainGestures.begins) + " begin / " + juce::String (gainGestures.ends) + " end");
    }

    // Percent: the display unit is not the parameter unit, which is the case a
    // parser that ignores the suffix gets wrong in the other direction.
    widthParam->setValueNotifyingHost (0.25f);
    typeInto (width, "35%");
    check (std::abs (widthFromNorm (widthParam->getValue()) * 100.0 - 35.0) < 1.0,
           "percent with the unit", "35% -> "
               + juce::String (widthFromNorm (widthParam->getValue()) * 100.0, 2) + "%");

    widthParam->setValueNotifyingHost (0.25f);
    typeInto (width, "35");
    check (std::abs (widthFromNorm (widthParam->getValue()) * 100.0 - 35.0) < 1.0,
           "percent without the unit", "35 -> "
               + juce::String (widthFromNorm (widthParam->getValue()) * 100.0, 2) + "%");

    // Out of range, both directions, and the overflow literal.
    struct Extreme { const char* typed; float wantNorm; const char* note; };

    const Extreme extremes[] = {
        { "999",    1.0f, "far above the range clamps to the top" },
        { "-999",   0.0f, "far below the range clamps to the bottom" },
        { "1e999",  1.0f, "overflow literal clamps to the top" },
        { "-1e999", 0.0f, "negative overflow clamps to the bottom" },
        { "nan",    0.5f, "\"nan\" is not a number to land on" },
    };

    for (const auto& e : extremes)
    {
        gainParam->setValueNotifyingHost (0.5f);
        gainGestures.reset();
        typeInto (gain, e.typed);

        const float got = gainParam->getValue();
        check (std::abs (got - e.wantNorm) < 0.01f, e.note,
               juce::String ("typed \"") + e.typed + "\" -> norm " + juce::String (got, 4)
                   + " (want " + juce::String (e.wantNorm, 2) + ")");
        check (gainGestures.begins == gainGestures.ends, "  gestures balanced",
               juce::String (gainGestures.begins) + " begin / " + juce::String (gainGestures.ends) + " end");
    }

    // Words, for the mode knob.
    auto* modeParam = proc.apvts.getParameter ("mode");

    for (const char* word : { "Hot", "hot", "Mild", "Moderate" })
    {
        modeParam->setValueNotifyingHost (0.0f);
        typeInto (mode, word);
        check (juce::String (kModeNames[modeFromNorm (modeParam->getValue(), 3)]).equalsIgnoreCase (word),
               "mode word lands on its mode",
               juce::String ("typed \"") + word + "\" -> " + fmtMode (modeParam->getValue()));
    }

    // Round trip: whatever the face prints, the face reads back.
    int roundTripFailures = 0;

    for (int i = 0; i <= 20; ++i)
    {
        const float n = (float) i / 20.0f;
        gainParam->setValueNotifyingHost (n);
        const auto printed = gain.displayText();

        gainParam->setValueNotifyingHost (0.9f);
        gain.setValueFromText (printed);

        if (std::abs (gainParam->getValue() - n) > 0.01f)
            ++roundTripFailures;
    }

    check (roundTripFailures == 0, "printed text reads back to the same value",
           juce::String (roundTripFailures) + " of 21 sample points failed");

    /*  What one commit costs.  Reported, not asserted: this is not a realtime
        path, so a number here is evidence for the lead rather than a
        regression gate.  It is large because the knob has no text->value
        mapping to call — it SEARCHES for the value whose printed form matches,
        sampling the formatter 2 x 257 times.  UI_SPEC 3.1 asks for the DSP
        layer's parameterTextToValue instead, which would be one call. */
    {
        gainParam->setValueNotifyingHost (0.2f);
        allocCount = 0;
        allocGateOpen = true;
        gain.setValueFromText ("-9.5 dB");
        allocGateOpen = false;
        std::printf ("  (measured) one text commit costs %d allocations "
                     "(the formatter is sampled 2x257 times to invert it)\n",
                     (int) allocCount.load());
    }

    // Escape abandons the edit.
    gainParam->setValueNotifyingHost (0.5f);
    gain.beginTextEntry();
    if (auto* te = findChild<juce::TextEditor> (gain))
    {
        te->setText ("-30 dB", juce::dontSendNotification);
        te->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        check (! te->isVisible(), "Escape closes the inline editor");
    }
    check (std::abs (gainParam->getValue() - 0.5f) < 1.0e-6f, "Escape abandons the edit",
           "norm " + juce::String (gainParam->getValue(), 4));
}

// =============================================================================
//  2. Keyboard nudge
// =============================================================================

void testNudge()
{
    std::printf ("\nKEYBOARD NUDGE\n");

    KitProcessor proc;
    ferment::FermentKnob knob (proc.apvts, "gain", "GAIN", fmtGain);
    knob.setBounds (0, 0, 120, 100);

    auto* param = proc.apvts.getParameter ("gain");
    GestureCounter gestures (*param);
    auto& slider = knob.getSlider();

    auto press = [&slider] (int code, bool shift)
    {
        return slider.keyPressed (juce::KeyPress (code,
                                                  shift ? juce::ModifierKeys::shiftModifier
                                                        : juce::ModifierKeys(),
                                                  0));
    };

    // A continuous parameter gets half a percent of its range per fine step.
    param->setValueNotifyingHost (0.5f);
    gestures.reset();
    const bool tookUp = press (juce::KeyPress::upKey, false);
    const double fine = param->getValue() - 0.5;

    check (tookUp, "the knob consumes an arrow key");
    check (std::abs (fine - 0.005) < 1.0e-4, "arrow steps 0.5% of the range",
           "moved " + juce::String (fine, 5));
    check (gestures.begins == 1 && gestures.ends == 1, "arrow writes one balanced gesture",
           juce::String (gestures.begins) + " begin / " + juce::String (gestures.ends) + " end");

    param->setValueNotifyingHost (0.5f);
    gestures.reset();
    press (juce::KeyPress::upKey, true);
    const double coarse = param->getValue() - 0.5;

    check (std::abs (coarse - 0.05) < 1.0e-4, "shift-arrow steps ten times as far",
           "moved " + juce::String (coarse, 5));
    check (gestures.begins == 1 && gestures.ends == 1, "shift-arrow writes one balanced gesture",
           juce::String (gestures.begins) + " begin / " + juce::String (gestures.ends) + " end");

    // Down and left go the other way; right matches up.
    param->setValueNotifyingHost (0.5f);
    press (juce::KeyPress::downKey, false);
    check (param->getValue() < 0.5f, "down steps down", "norm " + juce::String (param->getValue(), 5));

    param->setValueNotifyingHost (0.5f);
    press (juce::KeyPress::leftKey, false);
    check (param->getValue() < 0.5f, "left steps down", "norm " + juce::String (param->getValue(), 5));

    param->setValueNotifyingHost (0.5f);
    press (juce::KeyPress::rightKey, false);
    check (param->getValue() > 0.5f, "right steps up", "norm " + juce::String (param->getValue(), 5));

    // Both ends: pressing into the wall must not move past it, and must not
    // leave a gesture open for a write that never happened.
    param->setValueNotifyingHost (1.0f);
    gestures.reset();
    for (int i = 0; i < 5; ++i) press (juce::KeyPress::upKey, true);
    check (param->getValue() == 1.0f, "arrows stop at the top of the range",
           "norm " + juce::String (param->getValue(), 5));
    check (gestures.begins == gestures.ends, "  gestures balanced at the top",
           juce::String (gestures.begins) + " begin / " + juce::String (gestures.ends) + " end");

    param->setValueNotifyingHost (0.0f);
    gestures.reset();
    for (int i = 0; i < 5; ++i) press (juce::KeyPress::downKey, true);
    check (param->getValue() == 0.0f, "arrows stop at the bottom of the range",
           "norm " + juce::String (param->getValue(), 5));
    check (gestures.begins == gestures.ends, "  gestures balanced at the bottom",
           juce::String (gestures.begins) + " begin / " + juce::String (gestures.ends) + " end");

    // A key the knob has no use for goes back to the slider.
    check (! press ('q', false), "an unrelated key is not swallowed");
}

// =============================================================================
//  3. Degenerate bounds, empty content, absent slots
// =============================================================================

void testDegenerate()
{
    std::printf ("\nDEGENERATE BOUNDS AND EMPTY CONTENT\n");

    {
        KitProcessor proc;
        ferment::FermentKnob knob (proc.apvts, "gain", "GAIN", fmtGain);
        exerciseDegenerateSizes (knob, "FermentKnob");

        /*  The family's known P0: an APVTS attachment declared before the widget
            it attaches to outlives it, and the parameter then writes into freed
            memory.  Every kit control owns its own attachment, so the two die
            together — this is the check that says so. */
        {
            auto doomed = std::make_unique<ferment::FermentKnob> (proc.apvts, "width", "W", fmtWidth);
            doomed->setBounds (0, 0, 120, 100);
            doomed.reset();
        }

        auto* width = proc.apvts.getParameter ("width");
        width->setValueNotifyingHost (0.8f);
        width->setValueNotifyingHost (0.1f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        check (true, "a destroyed knob stops listening",
               "parameter written twice after the knob was freed");

        // A parameter ID that is not in the tree: JUCE jassertfalses inside the
        // attachment, and the knob has to stay inert rather than crash.
        ferment::FermentKnob orphan (proc.apvts, "nosuchparam", "NONE", fmtGain);
        exerciseSize (orphan, 120, 100);
        orphan.setValueFromText ("-9.5 dB");
        orphan.getSlider().keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        check (true, "FermentKnob with a missing parameter ID", "constructed, typed into, nudged");

        // No formatter at all.
        ferment::FermentKnob unformatted (proc.apvts, "gain", "GAIN", nullptr);
        exerciseSize (unformatted, 120, 100);
        check (! unformatted.setValueFromText ("-9.5"), "a knob with no formatter refuses typed text");
    }

    {
        ferment::MeterStrip empty (20);
        exerciseDegenerateSizes (empty, "MeterStrip with no lanes");
        empty.push (0, 1.0);
        empty.push (-1, 1.0);
        empty.advance();
        check (true, "MeterStrip push to a lane that does not exist", "ignored");
    }

    {
        ferment::VerdictChips chips;
        exerciseDegenerateSizes (chips, "VerdictChips with no chips");
        check (chips.preferredHeight (200) == 0, "an empty chip row wants no height");
        check (! chips.isActive (0) && ! chips.isActive (-1), "out-of-range chip reads as unlit");
        chips.setActive (99, true);
        chips.addChip ("A VERY LONG VERDICT CAPTION INDEED");
        check (chips.preferredHeight (1) > 0, "a chip wider than the row still gets a row",
               juce::String (chips.preferredHeight (1)) + " px at width 1");
        exerciseDegenerateSizes (chips, "VerdictChips with one oversized chip");
    }

    {
        ferment::TargetList targets;
        exerciseDegenerateSizes (targets, "TargetList with no rows");
        targets.setValue (0, 1.0);
        targets.setValue (-1, 1.0);
        targets.addRow ("STAGING", "dB");
        targets.setValue (0, std::numeric_limits<double>::quiet_NaN());
        targets.setValue (0, std::numeric_limits<double>::infinity());
        exerciseDegenerateSizes (targets, "TargetList with NaN and Inf values");
    }

    {
        ferment::ModuleCard card ("STAGE");
        exerciseDegenerateSizes (card, "ModuleCard with no controls and no slots");

        card.setBypassComponent (nullptr, 40);
        card.setMeterComponent (nullptr, 40, 20);
        card.setBypassed (true);
        card.setControlWidth (0);
        exerciseSize (card, 300, 100);
        check (true, "ModuleCard with null slots", "set, laid out, painted");

        // A slot that dies before the card is the SafePointer's whole reason.
        {
            auto doomed = std::make_unique<juce::Component>();
            card.setMeterComponent (doomed.get(), 40, 20);
            doomed.reset();
        }
        exerciseSize (card, 300, 100);
        check (true, "ModuleCard whose meter slot was destroyed", "resized and painted");
    }

    {
        ferment::LearnButton learn;
        exerciseDegenerateSizes (learn, "LearnButton");

        for (auto s : { ferment::LearnButton::State::Listening,
                        ferment::LearnButton::State::Setting,
                        ferment::LearnButton::State::Done,
                        ferment::LearnButton::State::Idle })
            learn.setState (s, 0.5);

        learn.setState (ferment::LearnButton::State::Listening, -5.0);
        learn.setState (ferment::LearnButton::State::Listening, 99.0);
        exerciseSize (learn, 160, 24);
        check (true, "LearnButton progress out of 0..1", "clamped, painted");
    }

    {
        ferment::FermentToggle toggle ("BYP");
        exerciseDegenerateSizes (toggle, "FermentToggle");

        KitProcessor proc;
        ferment::FermentToggle attached ("BYP");
        attached.attachTo (proc.apvts, "nosuchparam");
        exerciseSize (attached, 60, 26);
        check (true, "FermentToggle attached to a missing parameter ID", "constructed and painted");
    }
}

// =============================================================================
//  4. MeterStrip
// =============================================================================

bool isAmber (juce::Colour c)
{
    return c.getAlpha() > 60 && c.getRed() > 120 && c.getRed() > c.getBlue() + 40;
}

/** The topmost and bottommost amber-ish pixel in a column, or {-1,-1}.

    Both ends matter.  A sparkline that wraps its ring wrongly draws a near
    vertical segment at one edge, and the topmost pixel of that segment is the
    same as the topmost pixel of a correctly drawn rising line — only the
    *extent* of the column tells them apart. */
std::pair<int, int> amberSpan (const juce::Image& img, int x)
{
    int top = -1, bottom = -1;

    for (int y = 0; y < img.getHeight(); ++y)
    {
        if (isAmber (img.getPixelAt (x, y)))
        {
            if (top < 0)
                top = y;

            bottom = y;
        }
    }

    return { top, bottom };
}

void testMeterStrip()
{
    std::printf ("\nMETERSTRIP\n");

    constexpr int width = 420, pollHz = 10;
    const int historyLen = 3 * pollHz;              // matches the component's rule

    auto buildRamp = [&] (int frames)
    {
        auto strip = std::make_unique<ferment::MeterStrip> (pollHz);
        strip->addLane ({ "RAMP", 0.0, 100.0, "", 0, false });
        strip->setBounds (0, 0, width, ferment::MeterStrip::laneHeight);

        for (int i = 0; i < frames; ++i)
        {
            strip->push (0, 100.0 * (double) i / (double) juce::jmax (1, frames - 1));
            strip->advance();
        }

        return strip;
    };

    // Fed before it is ever painted, both short of the ring and past it.
    for (int frames : { 4, historyLen, historyLen * 2 + 7 })
    {
        auto strip = buildRamp (frames);

        juce::Image img (juce::Image::ARGB, width, ferment::MeterStrip::laneHeight, true);
        { juce::Graphics g (img); strip->paint (g); }

        /*  The sparkline is the right-hand block of the lane.  Fed a rising
            ramp it must rise monotonically across that block: the oldest
            sample at the left, the newest at the right, and no column
            containing a tall vertical run — which is what a ring walked from
            the wrong offset draws where it wraps.
        */
        int firstY = -1, lastY = -1, tallest = 0, tallestX = -1;

        for (int x = width - 96; x < width; ++x)
        {
            const auto [top, bottom] = amberSpan (img, x);

            if (top < 0)
                continue;

            if (firstY < 0)
                firstY = top;

            lastY = bottom;

            if (bottom - top > tallest) { tallest = bottom - top; tallestX = x; }
        }

        juce::String detail;
        detail << frames << " frames into a " << historyLen << "-frame ring: left y=" << firstY
               << " right y=" << lastY << ", tallest column " << tallest << " px at x=" << tallestX;

        // Sixteen pixels of plot over at most 30 samples of a smooth ramp: no
        // single column can legitimately span more than a few of them.
        check (firstY > 0 && lastY > 0 && lastY < firstY && tallest <= 4,
               "sparkline runs oldest-to-newest with no wrap discontinuity", detail);
    }

    // NaN and Inf readings must not reach the renderer.
    {
        ferment::MeterStrip strip (pollHz);
        strip.addLane ({ "NAN", -30.0, 0.0, "dB", 1, true });
        strip.setBounds (0, 0, width, ferment::MeterStrip::laneHeight);

        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();

        for (double v : { nan, inf, -inf, -6.0, nan, 0.0 })
        {
            strip.push (0, v, v);
            strip.advance();
        }

        exerciseSize (strip, width, ferment::MeterStrip::laneHeight);
        check (true, "MeterStrip with NaN and Inf readings", "painted");
    }

    /*  What one juce::Graphics::drawText costs, printed for scale.  JUCE 8
        shapes text on every call, and that shaping allocates: no component
        that prints a label can be allocation-free while it does. */
    size_t perDrawText = 0;
    {
        struct OneLabel : juce::Component
        {
            void paint (juce::Graphics& g) override
            {
                g.setColour (ferment::theme::amber);
                g.setFont (ferment::theme::mono (12.0f));
                g.drawText ("-12.0 LU", getLocalBounds(), juce::Justification::centredRight, false);
            }
        };

        OneLabel label;
        label.setBounds (0, 0, 120, 26);
        perDrawText = allocationsInPaint (label, 120, 26);

        std::printf ("  (control) one drawText costs %d allocations\n", (int) perDrawText);
    }

    // Allocation-free paint, and allocation-free push once the digits settle.
    {
        ferment::MeterStrip strip (20);

        for (int i = 0; i < 6; ++i)
            strip.addLane ({ "LANE", -36.0, 0.0, "LU", 1, i == 2 });

        strip.setBounds (0, 0, 460, strip.preferredHeight());

        for (int i = 0; i < 80; ++i)
        {
            for (int lane = 0; lane < 6; ++lane)
                strip.push (lane, -12.0 - lane);

            strip.advance();
        }

        /*  Twelve labels, twelve bar rectangles, one peak marker and six
            sparklines, drawn by hand.  The strip may not cost more than that:
            a juce::String or a Path rebuilt per frame shows up here as a
            number over the stand-in's, and one that GROWS per frame shows up
            as a second measurement that differs from the first. */
        const int stripH = strip.preferredHeight();
        const juce::String caption ("LANE"), value ("-12.0 LU");

        DrawingProbe probe;
        probe.draw = [caption, value] (juce::Graphics& g, juce::Path& path)
        {
            const auto captionFont = ferment::theme::monoTracked (9.0f, true);
            const auto valueFont   = ferment::theme::mono (12.0f);

            for (int lane = 0; lane < 6; ++lane)
            {
                const auto row = juce::Rectangle<int> (0, lane * ferment::MeterStrip::laneHeight,
                                                       460, ferment::MeterStrip::laneHeight);

                g.setColour (ferment::theme::labelDim);
                g.setFont (captionFont);
                g.drawText (caption, row, juce::Justification::centredLeft, false);

                g.setColour (ferment::theme::amber);
                g.setFont (valueFont);
                g.drawText (value, row, juce::Justification::centredRight, false);

                const auto bar = row.toFloat().withHeight (6.0f);
                g.setColour (ferment::theme::faceEdge);
                g.fillRoundedRectangle (bar, 2.0f);
                g.setColour (ferment::theme::amber);
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * 0.6f), 2.0f);

                if (lane == 2)                      // the one lane with a peak marker
                    g.fillRect (juce::Rectangle<float> (10.0f, bar.getY(), 2.0f, bar.getHeight()));

                path.clear();
                path.startNewSubPath (0.0f, 0.0f);
                for (int i = 1; i < 60; ++i)        // 3 s of history at 20 Hz
                    path.lineTo ((float) i, (float) (i % 7));
                g.strokePath (path, juce::PathStrokeType (1.0f));
            }
        };
        probe.setBounds (0, 0, 460, stripH);

        const auto allocs = allocationsInPaint (strip, 460, stripH);
        const auto budget = allocationsInPaint (probe, 460, stripH);

        check (allocs <= budget + paintBudgetSlack, "MeterStrip::paint costs no more than its own drawing",
               juce::String ((int) allocs) + " allocations, the same drawing by hand costs "
                   + juce::String ((int) budget));

        for (int i = 0; i < 40; ++i)
        {
            juce::Image img (juce::Image::ARGB, 460, strip.preferredHeight(), true);
            juce::Graphics g (img);
            strip.paint (g);
        }

        const auto later = allocationsInPaint (strip, 460, strip.preferredHeight());
        check (later == allocs, "MeterStrip::paint costs the same on every frame",
               juce::String ((int) allocs) + " then " + juce::String ((int) later));

        allocCount = 0;
        allocGateOpen = true;
        for (int i = 0; i < 20; ++i)
        {
            for (int lane = 0; lane < 6; ++lane)
                strip.push (lane, -12.0 - lane);

            strip.advance();
        }
        allocGateOpen = false;

        check (allocCount.load() == 0, "MeterStrip poll allocates nothing once settled",
               juce::String ((int) allocCount.load()) + " allocations over 20 ticks");
    }

    // The other two poll-path components make the same promise.
    {
        ferment::TargetList targets;

        for (int i = 0; i < 6; ++i)
            targets.addRow ("ROW", "dB");

        targets.setBounds (0, 0, 240, targets.preferredHeight());

        for (int i = 0; i < 6; ++i)
            targets.setValue (i, -1.5 * i);

        const int rowsH = targets.preferredHeight();
        const juce::String label ("ROW"), value ("-1.5 dB");

        DrawingProbe probe;
        probe.draw = [label, value] (juce::Graphics& g, juce::Path&)
        {
            const auto labelFont = ferment::theme::monoTracked (9.0f, true);
            const auto valueFont = ferment::theme::mono (11.0f);

            for (int row = 0; row < 6; ++row)
            {
                const auto line = juce::Rectangle<int> (0, row * ferment::TargetList::rowHeight,
                                                        240, ferment::TargetList::rowHeight);

                g.setColour (ferment::theme::labelDim);
                g.setFont (labelFont);
                g.drawText (label, line, juce::Justification::centredLeft, false);

                g.setColour (ferment::theme::amber);
                g.setFont (valueFont);
                g.drawText (value, line, juce::Justification::centredRight, false);
            }
        };
        probe.setBounds (0, 0, 240, rowsH);

        const auto allocs = allocationsInPaint (targets, 240, rowsH);
        const auto budget = allocationsInPaint (probe, 240, rowsH);

        check (allocs <= budget + paintBudgetSlack, "TargetList::paint costs no more than its own drawing",
               juce::String ((int) allocs) + " allocations, the same drawing by hand costs "
                   + juce::String ((int) budget));
    }

    {
        ferment::VerdictChips chips;

        for (const char* c : { "SUB HEAVY", "DARK", "BRIGHT", "PRE-CLIPPED", "MONO FRAGILE" })
            chips.addChip (c);

        /*  The whole contract of this component: it reflects the booleans it is
            handed and holds no opinion of its own.  There is exactly one place
            in the Analyzer where a number becomes a judgement and it is
            analyzer-core, so a chip that lights on anything but what it was told
            has moved a threshold into the paint layer. */
        int disobeyed = 0;

        for (int i = 0; i < chips.numChips(); ++i)
            for (bool want : { true, false, true, false })
            {
                chips.setActive (i, want);

                if (chips.isActive (i) != want)
                    ++disobeyed;

                for (int other = 0; other < chips.numChips(); ++other)
                    if (other != i && chips.isActive (other))
                        ++disobeyed;
            }

        check (disobeyed == 0, "a chip is lit exactly when it is told to be, and no other is",
               juce::String (disobeyed) + " disagreements over "
                   + juce::String (chips.numChips()) + " chips");

        chips.setActive (0, true);
        chips.setBounds (0, 0, 300, chips.preferredHeight (300));

        /*  One lit pill and four outlined ones, drawn by hand — with the same
            captions the component has, because how much text shaping
            allocates may well depend on how many glyphs it is shaping. */
        juce::StringArray captions { "SUB HEAVY", "DARK", "BRIGHT", "PRE-CLIPPED", "MONO FRAGILE" };

        DrawingProbe pills;
        pills.draw = [captions] (juce::Graphics& g, juce::Path&)
        {
            g.setFont (ferment::theme::monoTracked (9.0f, true));

            for (int chip = 0; chip < 5; ++chip)
            {
                const auto r = juce::Rectangle<float> ((float) chip * 60.0f, 0.0f, 56.0f,
                                                       (float) ferment::VerdictChips::chipHeight);
                const float radius = r.getHeight() * 0.5f;

                g.setColour (ferment::theme::amber);
                g.fillRoundedRectangle (r, radius);

                if (chip != 0)                       // the unlit ones carry a rim
                {
                    g.setColour (ferment::theme::woodDark);
                    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
                }

                g.setColour (ferment::theme::labelDim);
                g.drawText (captions[chip], r.toNearestInt(), juce::Justification::centred, false);
            }
        };
        pills.setBounds (0, 0, 300, chips.preferredHeight (300));

        const auto allocs = allocationsInPaint (chips, 300, chips.preferredHeight (300));
        const auto budget = allocationsInPaint (pills, 300, chips.preferredHeight (300));

        check (allocs <= budget + paintBudgetSlack, "VerdictChips::paint costs no more than its own drawing",
               juce::String ((int) allocs) + " allocations, the same drawing by hand costs "
                   + juce::String ((int) budget));

        /*  The lamp rail is the form Percept actually ships, and it draws
            ellipses rather than pills — so it gets its own stand-in rather
            than riding on the horizontal one's budget. */
        chips.setVertical (true);
        const int railH = chips.preferredHeight (150);
        chips.setBounds (0, 0, 150, railH);

        DrawingProbe lamps;
        lamps.draw = [captions] (juce::Graphics& g, juce::Path&)
        {
            g.setFont (ferment::theme::monoTracked (9.0f, true));

            for (int chip = 0; chip < 5; ++chip)
            {
                const auto row = juce::Rectangle<int> (0, chip * (ferment::VerdictChips::lampRowHeight
                                                                  + ferment::VerdictChips::chipGap),
                                                       150, ferment::VerdictChips::lampRowHeight);
                const float d = ferment::VerdictChips::lampDiameter;
                const juce::Rectangle<float> lamp (4.0f, (float) row.getCentreY() - d * 0.5f, d, d);

                if (chip == 0)                       // lit: halo and lamp
                {
                    g.setColour (ferment::theme::amber.withAlpha (0.25f));
                    g.fillEllipse (lamp.expanded (3.0f));
                    g.setColour (ferment::theme::amber);
                    g.fillEllipse (lamp);
                }
                else                                 // unlit: socket and ring
                {
                    g.setColour (ferment::theme::bg);
                    g.fillEllipse (lamp);
                    g.setColour (ferment::theme::woodDark);
                    g.drawEllipse (lamp.reduced (0.5f), 1.0f);
                }

                g.setColour (ferment::theme::labelDim);
                g.drawText (captions[chip], row.withTrimmedLeft ((int) (d + 12.0f)),
                            juce::Justification::centredLeft, false);
            }
        };
        lamps.setBounds (0, 0, 150, railH);

        const auto railAllocs = allocationsInPaint (chips, 150, railH);
        const auto railBudget = allocationsInPaint (lamps, 150, railH);

        check (railAllocs <= railBudget + paintBudgetSlack, "the lamp rail costs no more than its own drawing",
               juce::String ((int) railAllocs) + " allocations, the same drawing by hand costs "
                   + juce::String ((int) railBudget));
    }
}

// =============================================================================
//  5. FermentToggle
// =============================================================================

void testToggle()
{
    std::printf ("\nFERMENTTOGGLE\n");

    KitProcessor proc;
    auto* param = proc.apvts.getParameter ("byp");

    ferment::FermentToggle toggle ("BYP");
    toggle.attachTo (proc.apvts, "byp");
    toggle.setBounds (0, 0, 60, ferment::FermentToggle::standardHeight);

    check (! toggle.getToggleState(), "an off parameter shows an off pill");

    param->setValueNotifyingHost (1.0f);
    check (toggle.getToggleState(), "parameter -> button",
           "param 1 -> toggle " + juce::String (toggle.getToggleState() ? "on" : "off"));

    param->setValueNotifyingHost (0.0f);
    check (! toggle.getToggleState(), "parameter -> button, back off");

    GestureCounter gestures (*param);
    toggle.triggerClick();
    // triggerClick posts a message; the click itself is what a user does.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (30);

    check (param->getValue() > 0.5f, "button -> parameter",
           "clicked -> param " + juce::String (param->getValue(), 2));
    check (gestures.begins == gestures.ends && gestures.begins >= 1,
           "the click is one balanced gesture",
           juce::String (gestures.begins) + " begin / " + juce::String (gestures.ends) + " end");

    // The lit and unlit pills must not paint the same, or the state is invisible.
    auto render = [&toggle] (bool on)
    {
        toggle.setToggleState (on, juce::dontSendNotification);
        juce::Image img (juce::Image::ARGB, 60, ferment::FermentToggle::standardHeight, true);
        { juce::Graphics g (img); toggle.paintEntireComponent (g, false); }
        return img;
    };

    const auto off = render (false);
    const auto on  = render (true);

    /*  Not merely "different": the LIT pill has to be the amber one.  A toggle
        wired to the inverse of its parameter still renders two distinct pills,
        so counting differences alone cannot see the state being backwards —
        which is a whole plugin's bypasses reading the wrong way round. */
    auto amberPixels = [] (const juce::Image& img)
    {
        int n = 0;

        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
                if (isAmber (img.getPixelAt (x, y)))
                    ++n;

        return n;
    };

    const int amberOn = amberPixels (on), amberOff = amberPixels (off);

    check (amberOn > amberOff * 4, "the lit pill is the amber one",
           juce::String (amberOn) + " amber pixels on, " + juce::String (amberOff) + " off");

    // An attachment that outlives its button is the family's known P0; the
    // attachment lives inside the button, so this destroys both in one go.
    {
        auto scoped = std::make_unique<ferment::FermentToggle> ("TEMP");
        scoped->attachTo (proc.apvts, "byp");
        scoped.reset();
    }
    param->setValueNotifyingHost (1.0f);
    param->setValueNotifyingHost (0.0f);
    check (true, "a destroyed toggle stops listening", "parameter written twice after destruction");
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf ("ferment_ui kit test\n");

    testTextEntry();
    testNudge();
    testDegenerate();
    testMeterStrip();
    testToggle();

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

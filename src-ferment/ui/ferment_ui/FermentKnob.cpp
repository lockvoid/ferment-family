#include "FermentKnob.h"

#include <cmath>
#include <functional>
#include <limits>

namespace ferment
{

namespace
{
    // Ratios from the brand mock, all relative to the 30-unit arc radius.
    // The arc's outer edge sits at 30 + 5.5/2 = 32.75, which is what a square
    // knob face has to contain.
    constexpr float mockArcRadius = 30.0f;
    constexpr float mockExtent    = 32.75f;
    constexpr float mockArcStroke = 5.5f;
    constexpr float mockCapRadius = 21.0f;
    constexpr float mockTickInner = 7.0f;
    constexpr float mockTickOuter = 18.0f;
    constexpr float mockTickWidth = 3.5f;

    /** The first number in a string, ignoring any units around it: "-9.5 dB",
        "35%" and "2:1" all yield their leading value.

        A literal too big for a double ("1e999") reads back as infinity, and an
        infinite target makes every candidate equally wrong — the scan below then
        kept its starting guess and the knob jumped silently to the BOTTOM of its
        range.  Saturating instead means "as far as it goes", which is what
        someone typing a huge number meant, and matches what "999" already did.

        The saturation point has to leave the differences between candidates
        representable: at 1e30 every |target - display| rounds to the same double
        and the scan is blind again.  A billion is past any display value a
        Ferment knob prints and keeps 1e-7 of resolution. */
    constexpr double numberLimit = 1.0e9;

    bool firstNumber (const juce::String& text, double& out)
    {
        auto p = text.getCharPointer();

        while (! p.isEmpty())
        {
            const auto c = *p;

            if (juce::CharacterFunctions::isDigit (c)
                || ((c == '-' || c == '+' || c == '.')
                    && juce::CharacterFunctions::isDigit (*(p + 1))))
            {
                // Parsed straight off the char pointer: juce::String(p) would
                // allocate a copy of the tail, and this runs once per grid
                // sample on both passes of the scan.
                const double value = juce::CharacterFunctions::getDoubleValue (p);

                if (std::isnan (value))
                    return false;

                out = juce::jlimit (-numberLimit, numberLimit, value);
                return true;
            }

            ++p;
        }

        return false;
    }

    bool hasDigit (const juce::String& text)
    {
        return text.containsAnyOf ("0123456789");
    }

    /** How finely valueForText() samples a knob's display mapping in order to
        invert it.  File scope rather than a local in that function: MSVC will
        not let a lambda read a function-local constexpr it has not captured,
        and capturing a compile-time constant to satisfy one compiler is the
        kind of thing that gets "cleaned up" later by someone building on
        clang. */
    constexpr int gridSteps = 256;
}

void FermentKnob::Rotary::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float side = juce::jmin (bounds.getWidth(), bounds.getHeight());

    if (side < 8.0f)
        return;

    const auto  centre  = bounds.getCentre();
    const float extent  = side * 0.5f;
    const float arcR    = extent * (mockArcRadius / mockExtent);
    const float arcW    = arcR   * (mockArcStroke / mockArcRadius);
    const float capR    = arcR   * (mockCapRadius / mockArcRadius);
    const float tickIn  = arcR   * (mockTickInner / mockArcRadius);
    const float tickOut = arcR   * (mockTickOuter / mockArcRadius);
    const float tickW   = arcR   * (mockTickWidth / mockArcRadius);

    const auto  rotary = getRotaryParameters();
    const float start  = rotary.startAngleRadians;
    const float end    = rotary.endAngleRadians;
    const float angle  = start + (float) valueToProportionOfLength (getValue()) * (end - start);

    auto strokeArc = [&] (float from, float to, juce::Colour colour)
    {
        if (std::abs (to - from) < 1.0e-4f)
            return;

        juce::Path p;
        p.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, from, to, true);
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (arcW, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    };

    strokeArc (start, end,   theme::woodShadow);
    strokeArc (start, angle, theme::amber);

    // Cap: dark disc, radial shade lifted slightly above centre so it reads as
    // top-lit, then the mock's dark outline and wood rim.
    const auto cap = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (centre);

    juce::ColourGradient shade (theme::faceTop,  centre.x, centre.y - capR * 0.35f,
                                theme::faceEdge, centre.x, centre.y + capR * 1.15f,
                                true);
    g.setGradientFill (shade);
    g.fillEllipse (cap);

    g.setColour (theme::bg.withAlpha (0.6f));
    g.drawEllipse (cap.expanded (0.5f), 1.0f);
    g.setColour (theme::woodDark);
    g.drawEllipse (cap, 1.0f);

    juce::Path tick;
    tick.startNewSubPath (0.0f, -tickOut);
    tick.lineTo (0.0f, -tickIn);
    g.setColour (theme::amber);
    g.strokePath (tick, juce::PathStrokeType (tickW, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded),
                  juce::AffineTransform::rotation (angle).translated (centre));
}

FermentKnob::Rotary::Rotary()
{
    // Arrow keys only reach a knob that can hold focus, and a knob only takes
    // focus if something gives it — the click that starts a drag is the natural
    // moment, so mouseDown does it below.
    setWantsKeyboardFocus (true);
}

void FermentKnob::Rotary::mouseDown (const juce::MouseEvent& e)
{
    // JUCE has no built-in fine-drag modifier for rotaries, so the drag extent
    // is stretched for the duration of the gesture instead.
    setMouseDragSensitivity (e.mods.isShiftDown() ? fineSensitivity : coarseSensitivity);
    grabKeyboardFocus();
    juce::Slider::mouseDown (e);
}

void FermentKnob::Rotary::mouseUp (const juce::MouseEvent& e)
{
    juce::Slider::mouseUp (e);
    setMouseDragSensitivity (coarseSensitivity);
}

bool FermentKnob::Rotary::keyPressed (const juce::KeyPress& key)
{
    /*  The key CODE, not the KeyPress.  KeyPress::operator== (int) is false
        whenever any modifier is held, so comparing against KeyPress::downKey
        matched a bare arrow and silently missed every shift-arrow — which is
        half the feature.
    */
    const int code = key.getKeyCode();

    const int steps = code == juce::KeyPress::upKey   || code == juce::KeyPress::rightKey ?  1
                    : code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey  ? -1
                                                                                          :  0;

    if (steps == 0 || ! onNudge)
        return juce::Slider::keyPressed (key);

    onNudge (steps, key.getModifiers().isShiftDown());
    return true;
}

bool FermentKnob::ValueEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::tabKey && onTabKey)
        return onTabKey();

    return juce::TextEditor::keyPressed (key);
}

FermentKnob::FermentKnob (juce::AudioProcessorValueTreeState& apvts,
                          const juce::String& paramID,
                          const juce::String& captionIn,
                          Formatter formatterIn)
    : caption (captionIn.toUpperCase()), formatter (std::move (formatterIn))
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f,
                                true);
    slider.setScrollWheelEnabled (true);
    addAndMakeVisible (slider);

    slider.onValueChange = [this] { refreshValueText(); };
    slider.onNudge = [this] (int steps, bool coarse) { nudge (steps, coarse); };

    // The attachment sets the range, the text conversions AND the double-click
    // return value (SliderParameterAttachment's ctor does the last one from the
    // parameter's own default), so there is nothing to configure by hand here.
    // A paramID that is not in the APVTS leaves the attachment null and the
    // knob inert — JUCE jassertfalses rather than crashing.
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, paramID, slider);

    param = apvts.getParameter (paramID);

    valueEditor.setMultiLine (false);
    valueEditor.setReturnKeyStartsNewLine (false);
    valueEditor.setTabKeyUsedAsCharacter (false);
    valueEditor.setSelectAllWhenFocused (true);
    valueEditor.setJustification (juce::Justification::centred);
    valueEditor.setBorder ({ 0, 0, 0, 0 });
    valueEditor.setIndents (0, 0);
    valueEditor.setFont (theme::mono (11.0f));
    valueEditor.setColour (juce::TextEditor::textColourId,             theme::amber);
    valueEditor.setColour (juce::TextEditor::backgroundColourId,       theme::faceEdge);
    valueEditor.setColour (juce::TextEditor::outlineColourId,          theme::woodDark);
    valueEditor.setColour (juce::TextEditor::focusedOutlineColourId,   theme::amber);
    valueEditor.setColour (juce::TextEditor::highlightColourId,        theme::amber.withAlpha (0.3f));
    valueEditor.setColour (juce::TextEditor::highlightedTextColourId,  theme::labelCream);
    valueEditor.setColour (juce::CaretComponent::caretColourId,        theme::amber);

    valueEditor.onReturnKey  = [this] { commitTextEntry(); endTextEntry(); };
    valueEditor.onEscapeKey  = [this] { endTextEntry(); };
    valueEditor.onFocusLost  = [this] { commitTextEntry(); endTextEntry(); };
    valueEditor.onTabKey     = [this]
    {
        auto* next = nextKnob();
        commitTextEntry();
        endTextEntry();

        if (next != nullptr && next != this)
            next->beginTextEntry();

        return true;
    };

    // Added invisible: the value row is painted by paint(), so the editor only
    // exists on screen while someone is typing into it.
    addChildComponent (valueEditor);

    refreshValueText();
}

FermentKnob::~FermentKnob() = default;

void FermentKnob::refreshValueText()
{
    auto text = formatter ? formatter (slider.getValue()) : juce::String();

    if (text != valueText)
    {
        valueText = text;
        repaint();
    }
}

void FermentKnob::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    auto captionRow = b.removeFromBottom (captionRowHeight);
    auto valueRow   = b.removeFromBottom (valueRowHeight);

    g.setColour (theme::amber);
    g.setFont (theme::mono (11.0f));
    g.drawText (valueText, valueRow, juce::Justification::centred, false);

    g.setColour (theme::labelDim);
    g.setFont (theme::monoTracked (10.0f, true));
    g.drawFittedText (caption, captionRow, juce::Justification::centred, 1, 0.75f);
}

void FermentKnob::resized()
{
    auto face = getLocalBounds().withTrimmedBottom (textHeight);
    const int side = juce::jmin (face.getWidth(), face.getHeight());
    slider.setBounds (face.withSizeKeepingCentre (side, side));

    valueEditor.setBounds (valueRow());
}

juce::Rectangle<int> FermentKnob::valueRow() const
{
    return getLocalBounds().removeFromBottom (textHeight).removeFromTop (valueRowHeight);
}

void FermentKnob::mouseDown (const juce::MouseEvent& e)
{
    // The face belongs to the slider, which is a child and gets its own events;
    // anything that reaches the knob itself and lands on the value row is a
    // request to type a number in.
    if (valueRow().contains (e.getPosition()))
        beginTextEntry();
}

// ---- text entry ---------------------------------------------------------

void FermentKnob::beginTextEntry()
{
    valueEditor.setText (valueText, juce::dontSendNotification);
    valueEditor.setVisible (true);
    valueEditor.grabKeyboardFocus();
    valueEditor.selectAll();
}

void FermentKnob::endTextEntry()
{
    if (! valueEditor.isVisible())
        return;

    valueEditor.setVisible (false);

    // Hand focus back to the knob, so the arrow keys keep working on the
    // control someone was just editing.
    slider.grabKeyboardFocus();
}

void FermentKnob::commitTextEntry()
{
    setValueFromText (valueEditor.getText());
}

bool FermentKnob::setValueFromText (const juce::String& text)
{
    double value = 0.0;

    if (! valueForText (text, formatter,
                        slider.getMinimum(), slider.getMaximum(), slider.getInterval(),
                        value))
        return false;

    setValueWithGesture (value);
    return true;
}

bool FermentKnob::valueForText (const juce::String& text, const Formatter& formatter,
                                double lo, double hi, double interval, double& valueOut)
{
    const auto typed = text.trim();

    if (typed.isEmpty() || formatter == nullptr || hi <= lo)
        return false;

    // The grid (gridSteps, above) is what makes this work for any formatter
    // without knowing what it does: sample the display mapping, then refine
    // around the best sample.
    auto valueAt = [lo, hi] (int i) { return lo + (hi - lo) * (double) i / (double) gridSteps; };

    /*  Which mode this knob is in is a property of the KNOB, not of what was
        typed: a knob that prints numbers wants a number, and a knob that prints
        words wants a word.  Deciding it from the typed string instead sent every
        unparseable entry into the word branch, where startsWithIgnoreCase
        matched a bare "-" against "-35.0 dB" and landed the parameter in the
        middle of its negative half. */
    const bool numericDisplay = hasDigit (formatter (valueAt (gridSteps / 2)));

    if (numericDisplay)
    {
        double typedNumber = 0.0;

        if (! firstNumber (typed, typedNumber))
            return false;

        auto errorAt = [&] (double v)
        {
            double n = 0.0;
            return firstNumber (formatter (v), n) ? std::abs (n - typedNumber)
                                                  : std::numeric_limits<double>::max();
        };

        auto scan = [&] (double from, double to)
        {
            double best = from, bestErr = std::numeric_limits<double>::max();

            for (int i = 0; i <= gridSteps; ++i)
            {
                const double v = from + (to - from) * (double) i / (double) gridSteps;
                const double err = errorAt (v);

                if (err < bestErr) { bestErr = err; best = v; }
            }

            return best;
        };

        const double coarse = scan (lo, hi);
        const double window = (hi - lo) / (double) gridSteps;
        valueOut = scan (juce::jmax (lo, coarse - window), juce::jmin (hi, coarse + window));
    }
    else
    {
        /*  Word mode: a mode knob prints "Mild" / "Moderate" / "Hot", so match
            the text instead and land in the MIDDLE of the matching run — the
            first sample that matches sits on the boundary with the mode below
            it, where a rounding hair either way picks the wrong one. */
        int first = -1, last = -1;

        for (int i = 0; i <= gridSteps; ++i)
        {
            const auto shown = formatter (valueAt (i)).trim();

            if (shown.equalsIgnoreCase (typed) || shown.startsWithIgnoreCase (typed))
            {
                if (first < 0) first = i;
                last = i;
            }
            else if (first >= 0)
            {
                break;
            }
        }

        if (first < 0)
            return false;

        valueOut = valueAt ((first + last) / 2);
    }

    if (interval > 0.0)
        valueOut = lo + interval * std::round ((valueOut - lo) / interval);

    valueOut = juce::jlimit (lo, hi, valueOut);
    return true;
}

// ---- writing ------------------------------------------------------------

void FermentKnob::setValueWithGesture (double newValue)
{
    if (param == nullptr)
    {
        slider.setValue (newValue, juce::sendNotificationSync);
        return;
    }

    const float norm = juce::jlimit (0.0f, 1.0f, param->convertTo0to1 ((float) newValue));

    if (std::abs (norm - param->getValue()) < 1.0e-7f)
        return;

    param->beginChangeGesture();
    param->setValueNotifyingHost (norm);
    param->endChangeGesture();
}

void FermentKnob::nudge (int steps, bool coarse)
{
    const double span = slider.getMaximum() - slider.getMinimum();

    if (span <= 0.0)
        return;

    // A parameter that declares a step size gets that step; a continuous one
    // gets half a percent of its range, which is a hair on any Ferment knob.
    const double interval = slider.getInterval();
    const double fine     = interval > 0.0 ? interval : span / 200.0;

    setValueWithGesture (juce::jlimit (slider.getMinimum(), slider.getMaximum(),
                                       slider.getValue() + fine * (coarse ? 10.0 : 1.0) * steps));
}

FermentKnob* FermentKnob::nextKnob()
{
    auto* root = getTopLevelComponent();

    if (root == nullptr)
        return nullptr;

    std::vector<FermentKnob*> knobs;

    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        if (auto* knob = dynamic_cast<FermentKnob*> (&c))
        {
            knobs.push_back (knob);
            return;                       // a knob never contains another knob
        }

        for (auto* child : c.getChildren())
            walk (*child);
    };

    walk (*root);

    for (size_t i = 0; i < knobs.size(); ++i)
        if (knobs[i] == this)
            return knobs[(i + 1) % knobs.size()];

    return nullptr;
}

int FermentKnob::faceSizeFor (juce::Rectangle<int> area, int columns, int rows)
{
    columns = juce::jmax (1, columns);
    rows    = juce::jmax (1, rows);

    const int byWidth  = area.getWidth() / columns - gutter;
    const int byHeight = (area.getHeight() - (rows - 1) * rowSpacing) / rows - textHeight;

    return juce::jlimit (minFaceSize, standardFaceSize, juce::jmin (byWidth, byHeight));
}

int FermentKnob::layoutGrid (juce::Rectangle<int> area, const KnobList& knobs, int columns)
{
    const int count = (int) knobs.size();

    if (count == 0)
        return standardFaceSize;

    columns = juce::jlimit (1, count, columns);
    const int rows = (count + columns - 1) / columns;

    const int face  = faceSizeFor (area, columns, rows);
    const int cellW = face + gutter;          // the gutter lives inside the cell
    const int cellH = face + textHeight;

    // Centre the block rather than letting it hug a corner, so a grid that does
    // not fill its area still reads as deliberate.
    const int gridW = cellW * columns;
    const int gridH = cellH * rows + (rows - 1) * rowSpacing;
    const auto grid = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), gridW),
                                                  juce::jmin (area.getHeight(), gridH));

    for (int i = 0; i < count; ++i)
        knobs[(size_t) i]->setBounds (grid.getX() + (i % columns) * cellW,
                                      grid.getY() + (i / columns) * (cellH + rowSpacing),
                                      cellW, cellH);

    return face;
}

} // namespace ferment

#include "FermentKnob.h"

#include <cmath>

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

void FermentKnob::Rotary::mouseDown (const juce::MouseEvent& e)
{
    // JUCE has no built-in fine-drag modifier for rotaries, so the drag extent
    // is stretched for the duration of the gesture instead.
    setMouseDragSensitivity (e.mods.isShiftDown() ? fineSensitivity : coarseSensitivity);
    juce::Slider::mouseDown (e);
}

void FermentKnob::Rotary::mouseUp (const juce::MouseEvent& e)
{
    juce::Slider::mouseUp (e);
    setMouseDragSensitivity (coarseSensitivity);
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

    // The attachment sets the range, the text conversions AND the double-click
    // return value (SliderParameterAttachment's ctor does the last one from the
    // parameter's own default), so there is nothing to configure by hand here.
    // A paramID that is not in the APVTS leaves the attachment null and the
    // knob inert — JUCE jassertfalses rather than crashing.
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, paramID, slider);

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

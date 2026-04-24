#include "WarmLookAndFeel.h"

namespace P = ferment::palette;

WarmLookAndFeel::WarmLookAndFeel()
{
    setColour(juce::Slider::rotarySliderFillColourId,    P::amber);
    setColour(juce::Slider::rotarySliderOutlineColourId, P::knobRim);
    setColour(juce::Slider::thumbColourId,               P::amber);
    setColour(juce::Slider::trackColourId,               P::knobRim);
    setColour(juce::Slider::backgroundColourId,          P::knobBodyDark);
    setColour(juce::Label::textColourId,                 P::labelCream);
    setColour(juce::ComboBox::backgroundColourId,        P::knobBodyDark);
    setColour(juce::ComboBox::textColourId,              P::labelCream);
    setColour(juce::ComboBox::outlineColourId,           P::knobRim);
    setColour(juce::ComboBox::arrowColourId,             P::amber);
    setColour(juce::ComboBox::buttonColourId,            P::knobBody);
    setColour(juce::PopupMenu::backgroundColourId,       P::chassis);
    setColour(juce::PopupMenu::textColourId,             P::labelCream);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, P::amberDim);
    setColour(juce::PopupMenu::highlightedTextColourId,  P::labelCream);
    setColour(juce::TextButton::buttonColourId,          P::knobBody);
    setColour(juce::TextButton::buttonOnColourId,        P::amberDim);
    setColour(juce::TextButton::textColourOffId,         P::labelDim);
    setColour(juce::TextButton::textColourOnId,          P::labelCream);
}

void WarmLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                        float sliderPos, float rotaryStart, float rotaryEnd,
                                        juce::Slider&)
{
    auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(3.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.45f;
    const auto  centre = bounds.getCentre();
    const float angle  = rotaryStart + sliderPos * (rotaryEnd - rotaryStart);

    // Outer rim
    g.setColour(P::knobRim.darker(0.25f));
    g.fillEllipse(centre.x - radius - 2, centre.y - radius - 2,
                  (radius + 2) * 2, (radius + 2) * 2);

    // Body gradient
    juce::ColourGradient body(P::knobBody,     centre.x, centre.y - radius * 0.4f,
                              P::knobBodyDark, centre.x, centre.y + radius * 0.8f, false);
    g.setGradientFill(body);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2);

    g.setColour(P::chassisEdge.withAlpha(0.6f));
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2, 1.0f);

    // Value arc
    {
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius + 1.5f, radius + 1.5f,
                          0.0f, rotaryStart, angle, true);
        g.setColour(P::amber.withAlpha(0.35f));
        g.strokePath(arc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
    }

    // Indicator
    juce::Path indicator;
    const float innerR = radius * 0.25f;
    const float outerR = radius * 0.85f;
    indicator.startNewSubPath(0.0f, -outerR);
    indicator.lineTo(0.0f, -innerR);
    g.setColour(P::amber);
    g.strokePath(indicator,
                 juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded),
                 juce::AffineTransform::rotation(angle).translated(centre));
}

void WarmLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                                        float sliderPos, float, float,
                                        juce::Slider::SliderStyle style,
                                        juce::Slider& slider)
{
    // Fallback to default for linear sliders — EQ uses rotary
    juce::LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, sliderPos, 0, 0, style, slider);
}

void WarmLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                    int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(1.0f);
    g.setColour(P::knobBodyDark);
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(P::knobRim);
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

    // Arrow
    const float arrowSize = 5.0f;
    auto arrowBounds = juce::Rectangle<float>(width - 14.0f, height * 0.5f - arrowSize * 0.5f,
                                               arrowSize * 2, arrowSize);
    juce::Path arrow;
    arrow.startNewSubPath(arrowBounds.getX(), arrowBounds.getY());
    arrow.lineTo(arrowBounds.getRight(), arrowBounds.getY());
    arrow.lineTo(arrowBounds.getCentreX(), arrowBounds.getBottom());
    arrow.closeSubPath();
    g.setColour(P::amber);
    g.fillPath(arrow);
    juce::ignoreUnused(box);
}

void WarmLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(6, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

void WarmLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                            const juce::Colour&,
                                            bool isHighlighted,
                                            bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    const bool on = button.getToggleState();
    auto fill = on ? P::amber : P::knobBodyDark;
    if (isDown || isHighlighted) fill = fill.brighter(0.08f);
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(on ? P::amber.darker(0.3f) : P::knobRim);
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
}

juce::Font WarmLookAndFeel::getLabelFont(juce::Label& l)
{
    auto f = juce::LookAndFeel_V4::getLabelFont(l);
    f.setHeight(11.0f);
    return f;
}

juce::Font WarmLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return juce::Font(11.0f);
}

juce::Font WarmLookAndFeel::getPopupMenuFont()
{
    return juce::Font(12.0f);
}

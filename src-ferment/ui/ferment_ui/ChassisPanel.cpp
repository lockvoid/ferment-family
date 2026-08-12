#include "ChassisPanel.h"

namespace ferment
{

ChassisPanel::ChassisPanel()
{
    setInterceptsMouseClicks (false, false);
}

juce::Rectangle<int> ChassisPanel::layoutFrame (juce::Rectangle<int> bounds,
                                                ChassisPanel& chassis,
                                                juce::Component& header)
{
    chassis.setBounds (bounds);

    auto inner = contentArea (bounds);
    header.setBounds (inner.removeFromTop (headerHeight));
    inner.removeFromTop (headerGap);

    return inner;
}

void ChassisPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    if (bounds.getWidth() < 4.0f || bounds.getHeight() < 4.0f)
        return;

    juce::ColourGradient face (theme::faceTop,    bounds.getCentreX(), bounds.getY(),
                               theme::faceBottom, bounds.getCentreX(), bounds.getBottom(),
                               false);
    g.setGradientFill (face);
    g.fillRect (bounds);

    g.setColour (theme::faceEdge);
    g.drawRect (bounds.reduced (1.0f), 2.0f);

    // The mock's hairline where the face catches the light — cream at a low
    // alpha rather than white, so the palette stays closed.
    g.setColour (theme::labelCream.withAlpha (0.07f));
    g.drawRect (bounds.reduced (2.5f), 1.0f);
}

} // namespace ferment

#include "HeaderBar.h"

#include <cmath>

namespace ferment
{

namespace
{
    const juce::String wordmark { "FERMENT" };
}

HeaderBar::HeaderBar (juce::String subtitleIn)
    : subtitle (subtitleIn.toUpperCase())
{
    setInterceptsMouseClicks (false, true);
}

void HeaderBar::setSlotComponent (juce::Component* newSlot, int width)
{
    if (auto* previous = slot.getComponent())
        removeChildComponent (previous);

    slot = newSlot;
    slotWidth = juce::jmax (0, width);

    if (newSlot != nullptr)
        addAndMakeVisible (*newSlot);

    resized();
    repaint();
}

void HeaderBar::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();

    if (slot.getComponent() != nullptr)
        b.removeFromRight (slotWidth);

    /*  The wordmark takes exactly what it measures.  A round reservation was
        twice that (160 px for 80 px of glyphs), which is invisible until an
        editor gets narrow and the subtitle — the part that actually has to
        fit — starts squeezing against a run of empty space. */
    const auto wordmarkFont = theme::monoTracked (18.0f, true);
    const int  wordmarkWidth = (int) std::ceil (
        juce::GlyphArrangement::getStringWidth (wordmarkFont, wordmark));

    g.setColour (theme::labelCream);
    g.setFont (wordmarkFont);
    g.drawText (wordmark, b.removeFromLeft (juce::jmin (b.getWidth(), wordmarkWidth)),
                juce::Justification::centredLeft, false);

    if (subtitle.isNotEmpty() && b.getWidth() > 0)
    {
        b.removeFromLeft (12);
        g.setColour (theme::amber);
        g.setFont (theme::monoTracked (13.0f));
        g.drawFittedText (subtitle, b, juce::Justification::centredRight, 1, 0.7f);
    }
}

void HeaderBar::resized()
{
    if (auto* s = slot.getComponent())
        s->setBounds (getLocalBounds().removeFromRight (slotWidth));
}

} // namespace ferment

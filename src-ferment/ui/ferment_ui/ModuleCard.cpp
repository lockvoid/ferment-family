#include "ModuleCard.h"

namespace ferment
{

ModuleCard::ModuleCard (const juce::String& titleIn)
    : title (titleIn.toUpperCase())
{
}

void ModuleCard::addControl (juce::Component& control)
{
    controls.push_back (&control);
    addAndMakeVisible (control);
    resized();
}

void ModuleCard::setBypassComponent (juce::Component* bypass, int width)
{
    bypassSlot  = bypass;
    bypassWidth = width;

    if (bypass != nullptr)
        addAndMakeVisible (*bypass);

    resized();
}

void ModuleCard::setMeterComponent (juce::Component* meter, int width, int height)
{
    meterSlot   = meter;
    meterWidth  = width;
    meterHeight = height;

    if (meter != nullptr)
        addAndMakeVisible (*meter);

    resized();
}

void ModuleCard::setControlWidth (int width)
{
    if (controlWidth == width)
        return;

    controlWidth = juce::jmax (0, width);
    resized();
}

void ModuleCard::setBypassed (bool shouldBeBypassed)
{
    if (bypassed == shouldBeBypassed)
        return;

    bypassed = shouldBeBypassed;

    for (auto* c : controls)
        c->setAlpha (bypassed ? 0.4f : 1.0f);

    if (auto* meter = meterSlot.getComponent())
        meter->setAlpha (bypassed ? 0.4f : 1.0f);

    repaint();
}

void ModuleCard::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    if (bounds.getWidth() < 4.0f || bounds.getHeight() < 4.0f)
        return;

    /*  A card wears the same face as the standalone plugin it stands for: the
        chassis gradient, the 2 px edge, the cream hairline.  Master is a rack,
        and the units mounted in it should look like the units you already own —
        so the host gets its own colour instead (the editor paints a faceEdge
        well behind these), and the plugin faces stay exactly as they ship.

        Drawn from the same tokens as ChassisPanel rather than by borrowing the
        component, because a kit component may not include another one.
    */
    juce::ColourGradient face (theme::faceTop,    bounds.getCentreX(), bounds.getY(),
                               theme::faceBottom, bounds.getCentreX(), bounds.getBottom(),
                               false);
    g.setGradientFill (face);
    g.fillRect (bounds);

    g.setColour (theme::faceEdge);
    g.drawRect (bounds.reduced (1.0f), 2.0f);

    g.setColour (theme::labelCream.withAlpha (0.07f));
    g.drawRect (bounds.reduced (2.5f), 1.0f);

    // Title rule: the card reads as a unit because the caption sits in its own
    // band, not because the border is heavier.
    auto titleArea = getLocalBounds().removeFromTop (titleHeight);

    g.setColour (theme::faceEdge);
    g.fillRect (titleArea.getX() + padding, titleArea.getBottom() - 1,
                titleArea.getWidth() - padding * 2, 1);

    g.setColour (bypassed ? theme::labelDim : theme::labelCream);
    g.setFont (theme::monoTracked (10.0f, true));
    g.drawText (title, titleArea.reduced (padding, 0),
                juce::Justification::centredLeft, false);
}

void ModuleCard::resized()
{
    auto bounds = getLocalBounds();
    auto titleArea = bounds.removeFromTop (titleHeight).reduced (padding, 2);

    if (auto* bypass = bypassSlot.getComponent())
        bypass->setBounds (titleArea.removeFromRight (bypassWidth));

    auto body = bounds.reduced (padding, padding / 2);

    if (auto* meter = meterSlot.getComponent())
    {
        auto slot = body.removeFromRight (meterWidth);

        meter->setBounds (meterHeight > 0
                              ? slot.withSizeKeepingCentre (meterWidth,
                                                            juce::jmin (slot.getHeight(), meterHeight))
                              : slot);

        body.removeFromRight (padding / 2);
    }

    if (controls.empty())
        return;

    const int count = (int) controls.size();
    const int cell = controlWidth > 0 ? controlWidth : body.getWidth() / count;

    // Centred rather than left-packed, so a card wider than its controls need
    // reads as deliberate spacing instead of as a layout that ran out.
    auto run = body.withSizeKeepingCentre (juce::jmin (body.getWidth(), cell * count),
                                           body.getHeight());

    for (auto* control : controls)
        control->setBounds (run.removeFromLeft (cell));
}

} // namespace ferment

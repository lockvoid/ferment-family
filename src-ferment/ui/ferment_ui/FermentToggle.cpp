#include "FermentToggle.h"

namespace ferment
{

FermentToggle::FermentToggle (const juce::String& text)
    : juce::Button (text)
{
    setButtonText (text);
}

FermentToggle::~FermentToggle() = default;

void FermentToggle::attachTo (juce::AudioProcessorValueTreeState& apvts,
                              const juce::String& paramID)
{
    setClickingTogglesState (true);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, paramID, *this);
}

void FermentToggle::paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                                 bool shouldDrawButtonAsDown)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();

    auto fill = on ? theme::amber : theme::faceEdge;

    if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted)
        fill = fill.brighter (0.08f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, cornerRadius);

    // Lit buttons carry no outline — the amber fill is the state, and a darker
    // rim around it only softens the edge that is doing the talking.
    if (! on)
    {
        g.setColour (theme::woodDark);
        g.drawRoundedRectangle (bounds, cornerRadius, 1.0f);
    }

    /*  The text metrics below are LookAndFeel_V4::drawButtonText's, kept rather
        than reinvented: this component replaced a LookAndFeel that delegated the
        label to it, and the Utility editor's shipped screenshots are the
        regression test for that swap.  Any prettier arithmetic here would move
        pixels in an editor this change was not supposed to touch.
    */
    const auto font = theme::monoTracked (10.0f, true);
    g.setFont (font);
    g.setColour ((on ? theme::faceEdge : theme::labelDim)
                     .withMultipliedAlpha (isEnabled() ? 1.0f : 0.5f));

    const int yIndent    = juce::jmin (4, proportionOfHeight (0.3f));
    const int cornerSize = juce::jmin (getHeight(), getWidth()) / 2;
    const int fontHeight = juce::roundToInt (font.getHeight() * 0.6f);
    const int leftIndent  = juce::jmin (fontHeight, 2 + cornerSize / (isConnectedOnLeft()  ? 4 : 2));
    const int rightIndent = juce::jmin (fontHeight, 2 + cornerSize / (isConnectedOnRight() ? 4 : 2));
    const int textWidth   = getWidth() - leftIndent - rightIndent;

    if (textWidth > 0)
        g.drawFittedText (getButtonText(),
                          leftIndent, yIndent, textWidth, getHeight() - yIndent * 2,
                          juce::Justification::centred, 2);
}

} // namespace ferment

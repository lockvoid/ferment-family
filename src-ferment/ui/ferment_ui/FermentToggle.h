#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "FermentTheme.h"

#include <memory>

namespace ferment
{

/** The family's on/off control: a lit amber pill when on, a dark outlined one
    when off, mono ALL-CAPS label.

    This started as a private LookAndFeel inside the Utility editor, on the rule
    that the kit gets a button the day a second editor needs one.  Master's five
    module bypasses are that second editor, so it is a component now.

    It derives from juce::Button and paints itself rather than carrying a
    LookAndFeel: a LookAndFeel has to be un-installed from every button before
    either side is destroyed, and that hand-written cleanup loop is exactly the
    sort of thing that outlives the person who remembered why it was there.
*/
class FermentToggle : public juce::Button
{
public:
    explicit FermentToggle (const juce::String& text = {});
    ~FermentToggle() override;

    /** Binds this toggle to a bool APVTS parameter and makes clicks toggle it.
        The attachment is owned here, so it dies with the button. */
    void attachTo (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID);

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;

    /** The height a toggle gets in a Ferment editor unless it is in a bar. */
    static constexpr int standardHeight = 26;

    static constexpr float cornerRadius = 3.0f;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FermentToggle)
};

} // namespace ferment

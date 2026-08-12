#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "FermentTheme.h"

namespace ferment
{

/** Ferment Master's hero control: press it, play the loudest section, and the
    chain sets itself.

    The button owns no policy — it is told which phase it is in and how far
    through phase one it has got, and it draws that.  It owns a Timer only for
    the flash that follows a completed Learn, which is animation rather than
    state, and it stops as soon as the flash is over.
*/
class LearnButton : public juce::Button,
                    private juce::Timer
{
public:
    enum class State
    {
        Idle,        ///< "LEARN"
        Listening,   ///< phase 1: progress ring around the caption
        Setting,     ///< phase 2: the core is measuring the coloured signal
        Done         ///< a short amber flash, then back to Idle
    };

    LearnButton();
    ~LearnButton() override;

    /** @param progress 0..1, used only in Listening. */
    void setState (State newState, double progress = 0.0);

    State getState() const noexcept { return state; }

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;

    /** How long the completion flash runs before the button returns to Idle. */
    static constexpr double flashMs = 900.0;

    /** The family's button radius. Deliberately the same number FermentToggle
        uses rather than a reference to it — a kit component may not include
        another one — so the hero control is shaped like every other button on
        the face and only its size and its colour say it is the important one. */
    static constexpr float cornerRadius = 3.0f;

private:
    void timerCallback() override;
    juce::String captionFor (State) const;

    State state = State::Idle;
    double progress = 0.0;
    double flashStartMs = 0.0;
    float flash = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LearnButton)
};

} // namespace ferment

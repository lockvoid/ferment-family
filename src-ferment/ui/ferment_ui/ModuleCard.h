#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "FermentTheme.h"

#include <vector>

namespace ferment
{

/** One module of Ferment Master: a titled card holding a row of controls, with
    a bypass slot in its title bar and a meter slot at the end of its row.

    The slots take plain Components rather than knobs and toggles, which is the
    same choice HeaderBar made and for the same reason: a card that knew about
    FermentKnob would have to know about every control the family ever adds, and
    the kit's components are supposed to be independent of one another.  Slot
    components stay owned by the editor, and are held weakly here so that a
    resize landing between two destructors reads as "no slot" rather than as
    freed memory.
*/
class ModuleCard : public juce::Component
{
public:
    explicit ModuleCard (const juce::String& title);

    /** Appends a control to the card's row.  Controls are laid out as equal
        cells, so a knob gets the same face size as its neighbours. */
    void addControl (juce::Component& control);

    /** Flush right in the title bar. */
    void setBypassComponent (juce::Component* bypass, int width);

    /** Flush right in the control row.  @a height 0 fills the row; a dial wants
        its own aspect rather than the whole column, so it passes one. */
    void setMeterComponent (juce::Component* meter, int width, int height = 0);

    /** Dims the card's contents when the module is bypassed — the toggle says
        so, but a card that still looks live is the one people misread. */
    void setBypassed (bool);

    /** Fixes the width of each control and centres the run.

        Left to itself every card divides its own body by its own control count,
        which is how the family ended up with knobs from 51 px to 120 px before
        FermentKnob::layoutGrid existed.  An editor with cards of different
        widths has the same problem one level up, so it works out one control
        width for the whole face and hands it to every card.  0 restores the
        fill-the-body behaviour. */
    void setControlWidth (int width);

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int titleHeight = 20;
    static constexpr int padding     = 8;

    /** The height a card needs for one row of standard-size knobs. */
    static constexpr int heightForKnobRow (int knobHeight)
    {
        return titleHeight + padding + knobHeight;
    }

private:
    juce::String title;
    std::vector<juce::Component*> controls;
    juce::Component::SafePointer<juce::Component> bypassSlot, meterSlot;
    int bypassWidth = 0, meterWidth = 0, meterHeight = 0, controlWidth = 0;
    bool bypassed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleCard)
};

} // namespace ferment

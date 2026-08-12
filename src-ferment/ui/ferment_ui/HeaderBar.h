#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "FermentTheme.h"

namespace ferment
{

/** The strip across the top of every Ferment editor: the FERMENT wordmark on
    the left, the product subtitle on the right, and an optional slot on the
    far right for whatever readout that plugin wants there.

    The slot takes a component rather than a string so Clip can put its SHAVE
    meter in it and Limit its GR text without either being special-cased here.
*/
class HeaderBar : public juce::Component
{
public:
    /** e.g. "CHARGE / HIGH OCTANE LEVELLING COMPRESSOR". Upper-cased for you. */
    explicit HeaderBar (juce::String subtitle);

    /** The slot component is owned by the caller; pass nullptr to clear it.
        It is added as a child of the bar and laid out flush right.

        Callers routinely declare the slot as a member AFTER the HeaderBar it
        goes into, so the slot dies first.  JUCE's ~Component unparents it but
        cannot reach into this class, so the reference is weak rather than raw:
        a stale slot reads as "no slot" instead of as freed memory. */
    void setSlotComponent (juce::Component* slot, int width);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String subtitle;
    juce::Component::SafePointer<juce::Component> slot;
    int slotWidth = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderBar)
};

} // namespace ferment

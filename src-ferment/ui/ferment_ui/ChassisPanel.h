#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "FermentTheme.h"

namespace ferment
{

/** The face of the rack unit: a vertical faceTop→faceBottom gradient with a
    2 px edge and a 1 px inner highlight.

    Corners are square, deliberately.  The chassis runs full-bleed and the host
    window is already rounded by the OS, so rounding here just puts a second,
    tighter curve inside the first and leaves a dark crescent in every corner.
    The brand mock agrees: its face rect carries no rx.  Window shape is the
    host's job.

    Mouse-transparent, so it can sit at the back of an editor without eating
    clicks meant for the controls on top of it.
*/
class ChassisPanel : public juce::Component
{
public:
    ChassisPanel();

    void paint (juce::Graphics&) override;

    /*  Editors run the chassis full-bleed.  The wood cheeks that used to sit
        either side are gone: hosts and macOS round the plugin window, which
        clipped them into an odd shape at exactly the edge where they were
        supposed to read as solid timber.
    */
    static constexpr int marginX = 20;
    static constexpr int marginY = 14;

    /** The content rectangle inside a full-bleed chassis — one definition of
        the editor margin rather than six copies. */
    static juce::Rectangle<int> contentArea (juce::Rectangle<int> bounds)
    {
        return bounds.reduced (marginX, marginY);
    }

    static constexpr int headerHeight = 26;
    static constexpr int headerGap    = 8;

    /** Seats the standard editor frame — chassis behind everything, header
        along the top — and returns the content area left underneath.

        All six editors opened with the identical five lines this replaces, so
        the header height and the gap below it existed as six copies of the same
        two numbers, free to drift apart.  Takes the header as a plain Component
        so this header need not know about HeaderBar. */
    static juce::Rectangle<int> layoutFrame (juce::Rectangle<int> bounds,
                                             ChassisPanel& chassis,
                                             juce::Component& header);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChassisPanel)
};

} // namespace ferment

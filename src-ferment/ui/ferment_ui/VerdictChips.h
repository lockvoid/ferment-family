#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "FermentTheme.h"

#include <vector>

namespace ferment
{

/** The Analyzer's verdict board: verdicts lit amber when they hold.

    Two shapes, one component.  The default is a wrapped horizontal run of
    pills sized to their captions.  setVertical() turns it into a lamp rail:
    every verdict becomes a round indicator lamp with its caption beside it,
    read top to bottom like the status lamps on a tape machine — the unlit
    lamps show what the analyzer *could* have said and chose not to.

    The component knows nothing about what a verdict means.  Captions are handed
    in once and each one is switched by a bool the caller reads straight off
    `Readout` — there is exactly one place in this product where a number becomes
    a judgement, and it is analyzer-core, not a paint routine.
*/
class VerdictChips : public juce::Component
{
public:
    VerdictChips() = default;

    /** Declares the chips, left to right.  Captions are upper-cased. */
    void addChip (const juce::String& caption);

    /** One lamp per row, caption beside it — the rail form. */
    void setVertical (bool shouldStack);

    /** Lights or clears one chip; repaints only if it changed. */
    void setActive (int index, bool active);

    /** Whether a chip is currently lit.  Out-of-range reads as not lit. */
    bool isActive (int index) const;

    int numChips() const { return (int) chips.size(); }

    void paint (juce::Graphics&) override;

    static constexpr int chipHeight = 20;
    static constexpr int chipGap    = 6;
    /** Row height in the lamp rail. */
    static constexpr int lampRowHeight = 24;
    static constexpr float lampDiameter = 9.0f;

    /** How tall this row needs to be to lay its chips out in @a width. */
    int preferredHeight (int width) const;

private:
    struct Chip
    {
        juce::String caption;
        int width = 0;
        bool active = false;
    };

    /** Runs the wrap (or the stack), calling @a place for each chip.  One
        definition shared by paint and the height query, so the two cannot
        disagree. */
    template <typename Fn>
    int layout (int width, Fn&& place) const;

    std::vector<Chip> chips;
    bool vertical = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VerdictChips)
};

} // namespace ferment

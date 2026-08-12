#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "FermentTheme.h"

#include <vector>

namespace ferment

{

/** A column of label / value rows — the Analyzer's target list, and the same
    instrument for any other "here are the numbers" panel.

    Rows carry a sign when asked to, because a target of +2.5 dB and one of
    -2.5 dB are opposite instructions and an unsigned "2.5" is the reading that
    gets a master wrong.

    Like MeterStrip, a row's text is formatted into a stack buffer and only
    committed to a juce::String when the digits change, so a settled panel costs
    nothing per poll.  Paint costs what its labels cost and no more; see the
    note in MeterStrip.h about why that is not zero.
*/
class TargetList : public juce::Component
{
public:
    TargetList() = default;

    /** @param label   the row caption
        @param suffix  printed after the number, e.g. "dB"
        @param signedValue  print a leading '+' for positive values */
    void addRow (const juce::String& label, const juce::String& suffix = "dB",
                 bool signedValue = true, int decimals = 1);

    void setValue (int row, double value);

    void paint (juce::Graphics&) override;

    static constexpr int rowHeight = 19;

    int preferredHeight() const { return rowHeight * (int) rows.size(); }

private:
    struct Row
    {
        juce::String label;
        juce::String suffix;
        bool signedValue = true;
        int decimals = 1;
        juce::String text;
    };

    std::vector<Row> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TargetList)
};

} // namespace ferment

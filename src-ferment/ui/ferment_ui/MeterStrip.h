#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "FermentTheme.h"

#include <limits>
#include <vector>

namespace ferment
{

/** The Analyzer's measurement block: one labelled horizontal lane per metric,
    each with a bar, an amber value and a 3 s history sparkline.

    Nothing here polls anything.  The editor owns the one Timer, reads the
    analyzer once per tick and pushes the numbers in, so the whole face is
    driven from a single coherent snapshot rather than from six components each
    sampling the core at a slightly different moment.

    The poll path does not allocate: the history is a fixed ring sized at
    construction and every value is formatted into a char buffer, turned into a
    juce::String only on the frames where the printed text actually changes.

    Paint adds nothing of its own — the sparkline reuses one Path — but it is
    not allocation-free, and cannot be: juce::Graphics::drawText shapes its text
    on every call and that shaping allocates (8 allocations per call, measured
    in tests/ferment_ui_test.cpp).  What the test pins is that the drawing costs
    no more than its labels do, and that the cost does not grow frame over
    frame.
*/
class MeterStrip : public juce::Component
{
public:
    /** @param pollHz how often the editor will call push()/advance(); this is
                      what sizes the three seconds of history. */
    explicit MeterStrip (int pollHz);

    struct Lane
    {
        juce::String caption;
        double lo = -30.0;          ///< value at the left end of the bar
        double hi = 0.0;            ///< value at the right end
        juce::String suffix;        ///< printed after the number, e.g. "LU"
        int decimals = 1;
        bool showPeak = false;      ///< draw the second pushed value as a marker
    };

    /** Lanes are declared once, up front — the history ring is sized to them. */
    void addLane (Lane lane);

    /** One metric of one frame.  @a peak is the lane's secondary reading (the
        running true-peak maximum, say) and is ignored unless the lane asked
        for it. */
    void push (int lane, double value, double peak = 0.0);

    /** Steps the history ring on by one frame.  Call once per poll, after
        every lane has been pushed. */
    void advance();

    void paint (juce::Graphics&) override;

    /** Two lines per lane: caption + bar, then the value on its own line. */
    static constexpr int laneHeight = 38;

    /** The height this strip wants for the lanes it has been given. */
    int preferredHeight() const { return laneHeight * (int) lanes.size(); }

private:
    struct LaneState
    {
        Lane spec;
        double value = 0.0;
        double peak = 0.0;
        juce::String text;      // cached; rebuilt only when the digits change
    };

    void refreshText (LaneState&);
    float normFor (const Lane&, double value) const;

    std::vector<LaneState> lanes;

    // history[lane * historyLen + i]; NaN marks a frame never written.
    std::vector<float> history;
    int historyLen = 0;
    int historyPos = 0;
    int historyFilled = 0;

    juce::Path sparkPath;       // reused across paints so it stops growing

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterStrip)
};

} // namespace ferment

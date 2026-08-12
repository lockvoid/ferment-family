#include "MeterStrip.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace ferment
{

namespace
{
    constexpr int captionWidth  = 66;
    constexpr int sparkWidth    = 96;
    constexpr int gap           = 8;
    constexpr int barHeight     = 6;
    /** The caption-and-bar line of a lane; what remains below is the value's. */
    constexpr int barLineHeight = 20;
}

MeterStrip::MeterStrip (int pollHz)
{
    // Three seconds of history, at whatever rate the editor polls.
    historyLen = juce::jlimit (8, 256, 3 * juce::jmax (1, pollHz));
}

void MeterStrip::addLane (Lane lane)
{
    lanes.push_back ({ std::move (lane), 0.0, 0.0, {} });
    history.resize (lanes.size() * (size_t) historyLen,
                    std::numeric_limits<float>::quiet_NaN());
    refreshText (lanes.back());
}

void MeterStrip::push (int lane, double value, double peak)
{
    if (! juce::isPositiveAndBelow (lane, (int) lanes.size()))
        return;

    auto& l = lanes[(size_t) lane];
    l.value = value;
    l.peak  = peak;
    refreshText (l);

    history[(size_t) lane * (size_t) historyLen + (size_t) historyPos] = (float) value;
}

void MeterStrip::advance()
{
    historyPos = (historyPos + 1) % historyLen;
    historyFilled = juce::jmin (historyLen, historyFilled + 1);
    repaint();
}

void MeterStrip::refreshText (LaneState& l)
{
    /*  Formatted into a stack buffer and compared before assigning: juce::String
        has no small-string optimisation, so building one per lane per frame
        would put an allocation on every poll tick for the life of the editor.
        A settled meter reprints the same digits, and this makes that free.
    */
    char buf[32];

    if (l.value <= -99.0)
        std::snprintf (buf, sizeof buf, "--");
    else
        std::snprintf (buf, sizeof buf, "%.*f", juce::jlimit (0, 3, l.spec.decimals), l.value);

    if (l.spec.suffix.isNotEmpty())
    {
        const auto used = std::strlen (buf);
        std::snprintf (buf + used, sizeof buf - used, " %s", l.spec.suffix.toRawUTF8());
    }

    if (l.text != buf)
        l.text = buf;
}

float MeterStrip::normFor (const Lane& lane, double value) const
{
    if (lane.hi <= lane.lo)
        return 0.0f;

    return (float) juce::jlimit (0.0, 1.0, (value - lane.lo) / (lane.hi - lane.lo));
}

void MeterStrip::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    const auto captionFont = theme::monoTracked (9.0f, true);
    const auto valueFont   = theme::mono (12.0f);

    for (size_t i = 0; i < lanes.size(); ++i)
    {
        const auto& l = lanes[i];
        auto row = area.removeFromTop (laneHeight);

        /*  Two lines per lane: caption and bar on top, the value alone on its
            own line beneath, right-aligned to where the bar ends.  On one line
            the four elements fought for the row; here the bar gets the width
            and the number gets the whitespace.  The sparkline keeps its own
            column on the right, spanning both lines — a trend reads better
            taller.  */
        auto sparkArea = row.removeFromRight (sparkWidth);
        row.removeFromRight (gap);

        auto barLine   = row.removeFromTop (barLineHeight);
        auto valueLine = row;

        g.setColour (theme::labelDim);
        g.setFont (captionFont);
        g.drawText (l.spec.caption, barLine.removeFromLeft (captionWidth),
                    juce::Justification::centredLeft, false);

        barLine.removeFromLeft (gap);

        g.setColour (theme::amber);
        g.setFont (valueFont);
        g.drawText (l.text, valueLine, juce::Justification::centredRight, false);

        // ---- bar ----
        {
            auto bar = barLine.withSizeKeepingCentre (barLine.getWidth(), barHeight).toFloat();

            g.setColour (theme::faceEdge);
            g.fillRoundedRectangle (bar, 2.0f);

            const float filled = normFor (l.spec, l.value) * bar.getWidth();

            if (filled > 1.0f)
            {
                g.setColour (theme::amber);
                g.fillRoundedRectangle (bar.withWidth (filled), 2.0f);
            }

            if (l.spec.showPeak)
            {
                const float x = bar.getX() + normFor (l.spec, l.peak) * bar.getWidth();
                g.setColour (theme::labelCream);
                g.fillRect (juce::Rectangle<float> (x - 1.0f, bar.getY() - 2.0f,
                                                    2.0f, bar.getHeight() + 4.0f));
            }
        }

        // ---- sparkline ----
        if (historyFilled > 1)
        {
            sparkPath.clear();          // keeps its storage, so this stops allocating

            const auto plot = sparkArea.reduced (0, 8).toFloat();
            bool started = false;

            for (int s = 0; s < historyFilled; ++s)
            {
                /*  Oldest first, so the newest sample sits at the right edge.
                    push() writes at historyPos and advance() steps past it, so
                    the newest frame is at historyPos - 1 and the oldest live one
                    at historyPos - historyFilled.  Starting one later than that
                    drew a saturated ring from second-oldest round to oldest: the
                    right-hand end of every settled sparkline was the OLDEST
                    sample, with a vertical drop into it.
                */
                const int idx = (historyPos - historyFilled + s + historyLen * 2) % historyLen;
                const float v = history[i * (size_t) historyLen + (size_t) idx];

                if (std::isnan (v))
                    continue;

                const float x = plot.getX() + plot.getWidth() * (float) s / (float) (historyFilled - 1);
                const float y = plot.getBottom() - plot.getHeight() * normFor (l.spec, v);

                if (! started) { sparkPath.startNewSubPath (x, y); started = true; }
                else            sparkPath.lineTo (x, y);
            }

            if (started)
            {
                g.setColour (theme::amber.withAlpha (0.85f));
                g.strokePath (sparkPath, juce::PathStrokeType (1.0f));
            }
        }
    }
}

} // namespace ferment

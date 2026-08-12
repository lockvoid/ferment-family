#include "VerdictChips.h"

namespace ferment
{

namespace
{
    constexpr float chipFontHeight = 9.0f;
    constexpr int   chipPadding    = 10;   // either side of the caption
}

void VerdictChips::addChip (const juce::String& caption)
{
    Chip chip;
    chip.caption = caption.toUpperCase();

    // Measured once, at declaration time: a chip's width never changes, and
    // measuring a string is not something to do inside paint.
    chip.width = juce::GlyphArrangement::getStringWidthInt (
                     theme::monoTracked (chipFontHeight, true), chip.caption)
               + chipPadding * 2;

    chips.push_back (std::move (chip));
    repaint();
}

void VerdictChips::setVertical (bool shouldStack)
{
    if (vertical == shouldStack)
        return;

    vertical = shouldStack;
    repaint();
}

void VerdictChips::setActive (int index, bool active)
{
    if (! juce::isPositiveAndBelow (index, (int) chips.size()))
        return;

    auto& chip = chips[(size_t) index];

    if (chip.active == active)
        return;

    chip.active = active;
    repaint();
}

bool VerdictChips::isActive (int index) const
{
    return juce::isPositiveAndBelow (index, (int) chips.size())
        && chips[(size_t) index].active;
}

template <typename Fn>
int VerdictChips::layout (int width, Fn&& place) const
{
    if (vertical)
    {
        int y = 0;

        for (const auto& chip : chips)
        {
            place (chip, juce::Rectangle<int> (0, y, width, lampRowHeight));
            y += lampRowHeight + chipGap;
        }

        return chips.empty() ? 0 : y - chipGap;
    }

    int x = 0, y = 0;

    for (const auto& chip : chips)
    {
        if (x > 0 && x + chip.width > width)
        {
            x = 0;
            y += chipHeight + chipGap;
        }

        place (chip, juce::Rectangle<int> (x, y, chip.width, chipHeight));
        x += chip.width + chipGap;
    }

    return chips.empty() ? 0 : y + chipHeight;
}

int VerdictChips::preferredHeight (int width) const
{
    return layout (width, [] (const Chip&, juce::Rectangle<int>) {});
}

void VerdictChips::paint (juce::Graphics& g)
{
    const auto font = theme::monoTracked (chipFontHeight, true);
    g.setFont (font);

    layout (getWidth(), [&] (const Chip& chip, juce::Rectangle<int> bounds)
    {
        if (vertical)
        {
            /*  A round indicator lamp with its caption beside it — the status
                lamps on a tape machine.  Lit: amber with a soft halo.  Unlit:
                a dark socket in a woodDark ring, so the board always shows
                everything the analyzer might say about a track.  */
            const float d  = lampDiameter;
            const float cx = 4.0f + d * 0.5f;
            const float cy = (float) bounds.getCentreY();

            juce::Rectangle<float> lamp (cx - d * 0.5f, cy - d * 0.5f, d, d);

            if (chip.active)
            {
                g.setColour (theme::amber.withAlpha (0.25f));
                g.fillEllipse (lamp.expanded (3.0f));
                g.setColour (theme::amber);
                g.fillEllipse (lamp);
                g.setColour (theme::labelCream);
            }
            else
            {
                g.setColour (theme::bg);
                g.fillEllipse (lamp);
                g.setColour (theme::woodDark);
                g.drawEllipse (lamp.reduced (0.5f), 1.0f);
                g.setColour (theme::labelDim);
            }

            g.drawText (chip.caption, bounds.withTrimmedLeft ((int) (d + 12.0f)),
                        juce::Justification::centredLeft, false);
            return;
        }

        const auto r = bounds.toFloat();
        const float radius = r.getHeight() * 0.5f;

        if (chip.active)
        {
            g.setColour (theme::amber);
            g.fillRoundedRectangle (r, radius);
            g.setColour (theme::faceEdge);
        }
        else
        {
            g.setColour (theme::faceEdge);
            g.fillRoundedRectangle (r, radius);
            g.setColour (theme::woodDark);
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
            g.setColour (theme::labelDim);
        }

        g.drawText (chip.caption, bounds, juce::Justification::centred, false);
    });
}

} // namespace ferment

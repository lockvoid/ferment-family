#include "TargetList.h"

#include <cstdio>

namespace ferment
{

void TargetList::addRow (const juce::String& label, const juce::String& suffix,
                         bool signedValue, int decimals)
{
    Row row;
    row.label       = label.toUpperCase();
    row.suffix      = suffix;
    row.signedValue = signedValue;
    row.decimals    = juce::jlimit (0, 3, decimals);
    rows.push_back (std::move (row));

    setValue ((int) rows.size() - 1, 0.0);
}

void TargetList::setValue (int index, double value)
{
    if (! juce::isPositiveAndBelow (index, (int) rows.size()))
        return;

    auto& row = rows[(size_t) index];

    char buf[32];
    std::snprintf (buf, sizeof buf, "%s%.*f%s%s",
                   row.signedValue && value >= 0.0 ? "+" : "",
                   row.decimals, value,
                   row.suffix.isEmpty() ? "" : " ", row.suffix.toRawUTF8());

    if (row.text == buf)
        return;

    row.text = buf;
    repaint();
}

void TargetList::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    const auto labelFont = theme::monoTracked (9.0f, true);
    const auto valueFont = theme::mono (11.0f);

    for (const auto& row : rows)
    {
        auto line = area.removeFromTop (rowHeight);

        g.setColour (theme::labelDim);
        g.setFont (labelFont);
        g.drawText (row.label, line, juce::Justification::centredLeft, false);

        g.setColour (theme::amber);
        g.setFont (valueFont);
        g.drawText (row.text, line, juce::Justification::centredRight, false);
    }
}

} // namespace ferment

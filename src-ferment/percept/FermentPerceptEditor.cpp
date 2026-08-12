#include "FermentPerceptEditor.h"
#include "FermentPerceptProcessor.h"

#include <cstdio>
#include <cstring>

namespace T = ferment::theme;
namespace FA = ferment::analyzer;

namespace
{
    /*  Three columns, read left to right the way the product works:
        MEASURE (meters + band share) -> ACT (targets + chain hint) ->
        VERDICT (a vertical rail of tags).  One column per question:
        what is it, what do I do, what does the analyzer call it.
    */
    constexpr int editorWidth  = 920;
    constexpr int editorHeight = 470;   // two-line meter lanes need the room

    constexpr int controlRowHeight   = 22;
    constexpr int actionColumnWidth  = 260;   // targets + chain hint
    constexpr int verdictColumnWidth = 150;   // the tag rail
    constexpr int columnGap        = 16;
    constexpr int blockGap         = 10;
    constexpr int sectionCaption   = 15;
    constexpr int bandRowHeight    = 96;      // taller than the old crammed 58

    // Meter lanes, in the order the design lists them.
    enum Lane { LufsI = 0, LufsS, TruePeak, Crest, Tilt, MonoLoss };

    // Target rows, straight off Readout::Targets.
    enum Target { Staging = 0, GainToTarget, TiltDelta, SubTrim, Width, Ceiling };

    // Verdict chips. The set is fixed; which ones light comes from the core.
    enum Chip { SubHeavy = 0, SubLeaning, Dark, Bright, PreClipped, Dense, MonoFragile };

    const char* satModeName(int mode)
    {
        switch (mode)
        {
            case 0:  return "Mild";
            case 1:  return "Moderate";
            default: return "Hot";
        }
    }

    const char* detectorHpName(int mode)
    {
        switch (mode)
        {
            case 0:  return "off";
            case 1:  return "100";
            default: return "300";
        }
    }

    const char* charModeName(int mode)
    {
        switch (mode)
        {
            case 0:  return "Fat";
            case 1:  return "Warm";
            default: return "Bright";
        }
    }

    void drawSectionCaption(juce::Graphics& g, juce::Rectangle<int> area, const char* text)
    {
        g.setColour(T::labelDim);
        g.setFont(T::monoTracked(9.0f, true));
        g.drawText(text, area, juce::Justification::centredLeft, false);
    }
}

// =============================================================================
//  Band share row
// =============================================================================

FermentPerceptEditor::BandShareRow::BandShareRow()
{
    setInterceptsMouseClicks(true, false);
}

void FermentPerceptEditor::BandShareRow::setShares(const double* newShares)
{
    bool changed = false;

    for (int i = 0; i < numBands; ++i)
    {
        if (std::abs(shares[(size_t)i] - newShares[i]) > 1.0e-4)
        {
            shares[(size_t)i] = newShares[i];
            changed = true;
        }
    }

    if (changed)
        repaint();
}

juce::Rectangle<int> FermentPerceptEditor::BandShareRow::barArea(int band) const
{
    const int cell = getWidth() / numBands;
    return { band * cell, 0, cell, getHeight() };
}

void FermentPerceptEditor::BandShareRow::paint(juce::Graphics& g)
{
    static const char* const captions[numBands] = { "20-60", "60-150", "150-400",
                                                    ".4-2k", "2-6k", "6-16k" };

    /*  Fixed full-scale rather than "tallest bar fills the box": the whole point
        of this row is that a sub share over 0.4 looks alarming, and a relative
        scale would draw a balanced mix exactly the same way as a sub-heavy one.
    */
    constexpr double fullScale = 0.55;

    const auto captionFont = T::monoTracked(8.0f, true);

    for (int i = 0; i < numBands; ++i)
    {
        auto cell = barArea(i).reduced(3, 0);
        auto caption = cell.removeFromBottom(11);

        g.setColour(T::faceEdge);
        g.fillRect(cell);

        const auto height = (int)std::round(juce::jlimit(0.0, 1.0, shares[(size_t)i] / fullScale)
                                            * cell.getHeight());

        if (height > 0)
        {
            g.setColour(i == hovered ? T::amber.brighter(0.2f) : T::amber);
            g.fillRect(cell.removeFromBottom(height));
        }

        g.setColour(i == hovered ? T::labelCream : T::labelDim);
        g.setFont(captionFont);
        g.drawText(captions[i], caption, juce::Justification::centred, false);
    }
}

void FermentPerceptEditor::BandShareRow::mouseMove(const juce::MouseEvent& e)
{
    const int cell = juce::jmax(1, getWidth() / numBands);
    const int band = juce::jlimit(0, numBands - 1, e.x / cell);

    if (band == hovered)
        return;

    hovered = band;

    static const char* const ranges[numBands] = { "20-60 Hz", "60-150 Hz", "150-400 Hz",
                                                  "400 Hz-2 kHz", "2-6 kHz", "6-16 kHz" };

    setTooltip(juce::String(ranges[band]) + "   "
               + juce::String(shares[(size_t)band] * 100.0, 1) + " %");
    repaint();
}

void FermentPerceptEditor::BandShareRow::mouseExit(const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

// =============================================================================
//  Hint panel
// =============================================================================

FermentPerceptEditor::HintPanel::HintPanel() = default;

void FermentPerceptEditor::HintPanel::setCollapsed(bool shouldBeCollapsed)
{
    if (collapsed == shouldBeCollapsed)
        return;

    collapsed = shouldBeCollapsed;
    repaint();
}

void FermentPerceptEditor::HintPanel::setRow(int index, const char* text, bool warn, bool label)
{
    if ((int)rows.size() <= index)
        rows.resize((size_t)index + 1);

    auto& row = rows[(size_t)index];

    if (row.warn == warn && row.label == label && std::strcmp(row.raw, text) == 0)
        return;

    std::snprintf(row.raw, sizeof row.raw, "%s", text);

    /*  fromUTF8, not the plain const char* constructor: the separator between
        settings is a middle dot, and juce::String reads a bare pointer as
        Latin-1, which turns every one of them into "Â·" on screen.
    */
    row.text = juce::String::fromUTF8(row.raw);
    row.warn = warn;
    row.label = label;
    repaint();
}

juce::String FermentPerceptEditor::HintPanel::text() const
{
    juce::String out = title;

    for (const auto& row : rows)
        out << '\n' << row.text;

    return out;
}

void FermentPerceptEditor::HintPanel::trimTo(int count)
{
    if ((int)rows.size() > count)
    {
        rows.resize((size_t)count);
        repaint();
    }
}

void FermentPerceptEditor::HintPanel::showChain(const ferment::policy::ChainSettings& s)
{
    if (title != "CHAIN HINT")
    {
        title = "CHAIN HINT";
        repaint();
    }

    // Module name on its own dim line, its values amber beneath — the same
    // label-above-value rhythm the meter lanes use.
    char buf[96];

    setLabel(0, "STAGE");
    std::snprintf(buf, sizeof buf, "%+.1f dB " "\xc2\xb7" " width %d%%",
                  s.stagingDb, (int)std::round(s.widthFactor * 100.0));
    setRow(1, buf);

    setLabel(2, "CHARGE");
    std::snprintf(buf, sizeof buf, "%s %.1f " "\xc2\xb7" " HP %s " "\xc2\xb7" " mix %d%%",
                  satModeName(s.charge.satMode), s.charge.saturation,
                  detectorHpName(s.charge.detectorHp), (int)std::round(s.charge.mixPct));
    setRow(3, buf);

    std::snprintf(buf, sizeof buf, "comp %.1f " "\xc2\xb7" " %s %.1f",
                  s.charge.compression, charModeName(s.charge.charMode), s.charge.character);
    setRow(4, buf);

    setLabel(5, "TONE");
    std::snprintf(buf, sizeof buf, "low %+.1f " "\xc2\xb7" " pres %+.1f " "\xc2\xb7" " high %+.1f",
                  s.eq.lowShelfDb, s.eq.presenceDb, s.eq.highShelfDb);
    setRow(6, buf);

    setLabel(7, "CLIP");
    std::snprintf(buf, sizeof buf, "ceil %.1f " "\xc2\xb7" " knee %d%% " "\xc2\xb7" " tilt %d%%",
                  s.clip.ceilingDb, (int)std::round(s.clip.kneePct), (int)std::round(s.clip.tiltPct));
    setRow(8, buf);

    setLabel(9, "LIMIT");
    std::snprintf(buf, sizeof buf, "ceil %.1f dBTP " "\xc2\xb7" " atk %d ms",
                  s.limit.ceilingDbtp, (int)std::round(s.limit.attackMs));
    setRow(10, buf);

    trimTo(11);
}

void FermentPerceptEditor::HintPanel::showVerification(const FA::Readout& r,
                                                        const FA::Profile& profile)
{
    if (title != "VERIFY")
    {
        title = "VERIFY";
        repaint();
    }

    char buf[96];

    const double lufs = r.loudnessReady ? r.lufsIntegrated : r.lufsShortTerm;
    const double lufsError = lufs - profile.targetLufs;
    const bool lufsOk = std::abs(lufsError) <= 0.5;

    setLabel(0, "LOUDNESS");
    std::snprintf(buf, sizeof buf, "%+.1f vs target " "\xc2\xb7" " %s",
                  lufsError, lufsOk ? "PASS" : "FAIL");
    setRow(1, buf, ! lufsOk);

    const bool tpOk = r.truePeakDb <= profile.ceilingDbtp + 0.1;
    setLabel(2, "TRUE PEAK");
    std::snprintf(buf, sizeof buf, "%.2f dBTP " "\xc2\xb7" " %s",
                  r.truePeakDb, tpOk ? "PASS" : "FAIL");
    setRow(3, buf, ! tpOk);

    /*  A master that measures clean can still overshoot once a lossy codec has
        reconstructed it, and -1.0 dBTP is where that starts to bite. Worth
        saying out loud rather than leaving as a pass. */
    const bool aacRisk = r.truePeakDb > -1.0;
    setLabel(4, "AAC RISK");
    setRow(5, aacRisk ? "TP over -1.0 dBTP" : "clear", aacRisk);

    setLabel(6, "CREST");
    std::snprintf(buf, sizeof buf, "%.1f dB", r.crestDb);
    setRow(7, buf);

    trimTo(8);
}

void FermentPerceptEditor::HintPanel::mouseDown(const juce::MouseEvent& e)
{
    if (e.y < titleHeight)
        setCollapsed(! collapsed);
}

void FermentPerceptEditor::HintPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    auto titleArea = bounds.removeFromTop(titleHeight);

    g.setColour(T::labelDim);
    g.setFont(T::monoTracked(9.0f, true));
    g.drawText(title, titleArea, juce::Justification::centredLeft, false);

    // A caret rather than a word: the row is small, and the direction is the
    // whole message.
    juce::Path caret;
    const float cx = (float)titleArea.getRight() - 8.0f;
    const float cy = (float)titleArea.getCentreY();

    if (collapsed)
    {
        caret.addTriangle(cx - 4.0f, cy - 3.0f, cx + 4.0f, cy - 3.0f, cx, cy + 3.0f);
    }
    else
    {
        caret.addTriangle(cx - 4.0f, cy + 3.0f, cx + 4.0f, cy + 3.0f, cx, cy - 3.0f);
    }

    g.setColour(T::amber);
    g.fillPath(caret);

    if (collapsed)
        return;

    for (const auto& row : rows)
    {
        auto line = bounds.removeFromTop(rowHeight);

        if (line.getBottom() > getHeight())
            break;

        if (row.label)
        {
            // The module name, dim and tracked like every section caption.
            g.setColour(T::labelDim);
            g.setFont(T::monoTracked(9.0f, true));
            g.drawText(row.text, line, juce::Justification::centredLeft, false);
        }
        else
        {
            // Its values, amber and indented a step under their name.
            g.setColour(row.warn ? T::labelCream : T::amber);
            g.setFont(T::mono(10.0f));
            g.drawText(row.text, line.withTrimmedLeft(10), juce::Justification::centredLeft, false);
        }
    }
}

// =============================================================================
//  Editor
// =============================================================================

FermentPerceptEditor::FermentPerceptEditor(FermentPerceptProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    addAndMakeVisible(chassis);
    addAndMakeVisible(header);

    meters.addLane({ "LUFS-I",    -36.0,  0.0, "LU",  1, false });
    meters.addLane({ "LUFS-S",    -36.0,  0.0, "LU",  1, false });
    meters.addLane({ "TRUE PEAK", -24.0,  3.0, "dBTP", 2, true });
    meters.addLane({ "CREST",       0.0, 24.0, "dB",  1, false });
    meters.addLane({ "TILT",      -30.0,  6.0, "dB",  1, false });
    meters.addLane({ "MONO LOSS",   0.0,  6.0, "dB",  2, false });
    addAndMakeVisible(meters);

    addAndMakeVisible(bands);

    chips.setVertical(true);       // the rail form: one full-width tag per verdict
    chips.addChip("SUB HEAVY");
    chips.addChip("SUB LEANING");
    chips.addChip("DARK");
    chips.addChip("BRIGHT");
    chips.addChip("PRE-CLIPPED");
    chips.addChip("ALREADY DENSE");
    chips.addChip("MONO FRAGILE");
    addAndMakeVisible(chips);

    targets.addRow("STAGING",        "dB");
    targets.addRow("GAIN TO TARGET", "dB");
    targets.addRow("TILT DELTA",     "dB");
    targets.addRow("SUB TRIM",       "dB");
    targets.addRow("WIDTH",          "",   false, 2);
    targets.addRow("CEILING",        "dBTP", false, 1);
    addAndMakeVisible(targets);

    addAndMakeVisible(hints);

    setupBar(modeBox, modeAtt, FermentPerceptProcessor::paramIDs()[FermentPerceptProcessor::kMode],
             { "Source", "Result" }, { &sourceBtn, &resultBtn });

    setupBar(profileBox, profileAtt, FermentPerceptProcessor::paramIDs()[FermentPerceptProcessor::kProfile],
             { "Studio", "Reel" }, { &studioBtn, &reelBtn });

    addAndMakeVisible(resetBtn);
    resetBtn.onClick = [this]
    {
        // The whole momentary pulse, inside one gesture. The processor listens
        // for the rising edge, so the parameter does not have to stay up long
        // enough for the audio thread to notice it, and it goes back to zero
        // immediately — which is what a momentary control should read as.
        if (auto* param = processor.apvts.getParameter(
                FermentPerceptProcessor::paramIDs()[FermentPerceptProcessor::kReset]))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost(1.0f);
            param->setValueNotifyingHost(0.0f);
            param->endChangeGesture();
        }
    };

    startTimerHz(pollHz);
    setSize(editorWidth, editorHeight);
}

FermentPerceptEditor::~FermentPerceptEditor() = default;

void FermentPerceptEditor::setupBar(
    juce::ComboBox& box,
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& attachment,
    const juce::String& paramID,
    const juce::StringArray& items,
    std::vector<ferment::FermentToggle*> buttons)
{
    for (int i = 0; i < items.size(); ++i)
        box.addItem(items[i], i + 1);

    addChildComponent(box);   // driven from the buttons; never shown

    for (size_t i = 0; i < buttons.size(); ++i)
    {
        auto* button = buttons[i];
        button->setClickingTogglesState(false);
        button->onClick = [&box, i] { box.setSelectedItemIndex((int)i, juce::sendNotificationSync); };
        addAndMakeVisible(*button);
    }

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, paramID, box);

    box.onChange = [&box, buttons]
    {
        const int selected = box.getSelectedItemIndex();

        for (size_t i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState((int)i == selected, juce::dontSendNotification);
    };

    box.onChange();
}

void FermentPerceptEditor::timerCallback()
{
    // One readout, one profile, one frame — every panel below describes the
    // same instant.
    const auto r = processor.readout();
    const auto profile = processor.activeProfile();

    meters.push(LufsI,    r.lufsIntegrated);
    meters.push(LufsS,    r.lufsShortTerm);
    meters.push(TruePeak, r.truePeakRecentDb, r.truePeakDb);
    meters.push(Crest,    r.crestDb);
    meters.push(Tilt,     r.tiltDb);
    meters.push(MonoLoss, r.monoLossDb);
    meters.advance();

    bands.setShares(r.bandShare);

    chips.setActive(SubHeavy,    r.subClass == FA::Readout::SubHeavy);
    chips.setActive(SubLeaning,  r.subClass == FA::Readout::SubLeaning);
    // Dark and bright are the sign of the core's own tilt target, not a
    // threshold invented here: it clamps the delta at zero once a mix is
    // already bright enough.
    chips.setActive(Dark,        r.spectrumReady && r.targets.tiltDeltaDb > 0.0);
    chips.setActive(Bright,      r.spectrumReady && r.tiltDb > profile.tiltTarget);
    chips.setActive(PreClipped,  r.preClipped);
    chips.setActive(Dense,       r.alreadyDense);
    chips.setActive(MonoFragile, r.monoFragile);

    /*  Only once there is something to measure: the staging target against
        silence is "-16 minus -300 = +284 dB", which is not advice, it is the
        gate value leaking through arithmetic.  loudnessReady latches, so a
        track that played and then stopped keeps showing its real targets.  */
    if (r.signalPresent || r.loudnessReady)
    {
        targets.setValue(Staging,      r.targets.driveDeltaDb);
        targets.setValue(GainToTarget, r.targets.gainToTargetDb);
        targets.setValue(TiltDelta,    r.targets.tiltDeltaDb);
        targets.setValue(SubTrim,      r.targets.subTrimDb);
        targets.setValue(Width,        r.targets.widthFactor);
        targets.setValue(Ceiling,      r.targets.ceilingDbtp);
    }

    if (processor.mode() == FermentPerceptProcessor::Result)
        refreshResultMode(r, profile);
    else
        refreshSourceMode(r, profile);
}

juce::String FermentPerceptEditor::hintText() const
{
    return hints.text();
}

void FermentPerceptEditor::refreshSourceMode(const FA::Readout& r, const FA::Profile& profile)
{
    // ferment-policy is the ONE translator from measurements to module values.
    // The hint panel renders its output; it does not have a mapping of its own.
    hints.showChain(ferment::policy::translate(r, profile));
}

void FermentPerceptEditor::refreshResultMode(const FA::Readout& r, const FA::Profile& profile)
{
    hints.showVerification(r, profile);
}

void FermentPerceptEditor::paint(juce::Graphics& g)
{
    g.fillAll(T::bg);
}

void FermentPerceptEditor::paintOverChildren(juce::Graphics& g)
{
    /*  Over the children, not under them: the chassis is a full-bleed child, and
        children paint after their parent — so a caption drawn in paint() is
        drawn and then immediately covered by the panel behind it.
    */
    drawSectionCaption(g, bandCaption,    "BAND SHARE");
    drawSectionCaption(g, verdictCaption, "VERDICT");
    drawSectionCaption(g, targetCaption,  "TARGETS");
}

void FermentPerceptEditor::resized()
{
    auto inner = ferment::ChassisPanel::layoutFrame(getLocalBounds(), chassis, header);

    // ---- control row: mode | profile | reset, flush right ----
    {
        auto row = inner.removeFromTop(controlRowHeight);
        inner.removeFromTop(blockGap);

        constexpr int buttonWidth = 62;

        resetBtn.setBounds(row.removeFromRight(buttonWidth).reduced(1));
        row.removeFromRight(12);
        reelBtn.setBounds(row.removeFromRight(buttonWidth).reduced(1));
        studioBtn.setBounds(row.removeFromRight(buttonWidth).reduced(1));
        row.removeFromRight(12);
        resultBtn.setBounds(row.removeFromRight(buttonWidth).reduced(1));
        sourceBtn.setBounds(row.removeFromRight(buttonWidth).reduced(1));
    }

    auto measure = inner.removeFromLeft(inner.getWidth() - actionColumnWidth
                                        - verdictColumnWidth - 2 * columnGap);
    inner.removeFromLeft(columnGap);
    auto action = inner.removeFromLeft(actionColumnWidth);
    inner.removeFromLeft(columnGap);
    auto verdict = inner;               // what remains is the rail

    // ---- MEASURE: the numbers, then where the energy sits ----
    meters.setBounds(measure.removeFromTop(meters.preferredHeight()));
    measure.removeFromTop(blockGap);

    bandCaption = measure.removeFromTop(sectionCaption);
    bands.setBounds(measure.removeFromTop(bandRowHeight));

    // ---- ACT: what to do about it ----
    targetCaption = action.removeFromTop(sectionCaption);
    targets.setBounds(action.removeFromTop(targets.preferredHeight()));
    action.removeFromTop(blockGap * 2);

    hints.setBounds(action);

    // ---- VERDICT: the tag rail ----
    verdictCaption = verdict.removeFromTop(sectionCaption);
    chips.setBounds(verdict.removeFromTop(chips.preferredHeight(verdict.getWidth())));
}

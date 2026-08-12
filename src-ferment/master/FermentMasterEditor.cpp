#include "FermentMasterEditor.h"

#include <cmath>
#include <cstdio>

namespace T = ferment::theme;
namespace FM = ferment::master;

using Proc = FermentMasterProcessor;

namespace
{
    /*  Every knob on this face is the family's standard size with the family's
        gutter around it, so the cell is one number and the rack is however wide
        that makes it.
    */
    constexpr int controlCell = ferment::FermentKnob::standardFaceSize
                              + ferment::FermentKnob::gutter;
    constexpr int controlHeight = ferment::FermentKnob::standardFaceSize
                                + ferment::FermentKnob::textHeight;

    constexpr int cardHeight   = ferment::ModuleCard::heightForKnobRow (controlHeight);
    constexpr int cardGap      = 10;

    /*  Margin of rack body visible around the mounted units.  Without it the
        cards fill the well edge to edge and Master's own colour only survives in
        the gaps between them, which is not enough to read as a host. */
    constexpr int rackPad      = 8;
    constexpr int bottomBar    = 30;
    constexpr int bottomGap    = 12;

    constexpr int editorWidth  = 1000;
    constexpr int editorHeight = ferment::ChassisPanel::marginY * 2
                               + ferment::ChassisPanel::headerHeight
                               + ferment::ChassisPanel::headerGap
                               + rackPad * 2 + cardHeight
                               + bottomGap + bottomBar;

    constexpr int bypassWidth = 34;
    constexpr int meterWidth  = 62;
    constexpr int meterHeight = 60;
    constexpr int readoutSlot = 250;

    juce::String fmtDb1(double v)      { return juce::String(v, 1) + " dB"; }
    juce::String fmtSignedDb(double v) { return (v >= 0 ? "+" : "") + juce::String(v, 1) + " dB"; }
    juce::String fmtKnob(double v)     { return juce::String(v, 1); }
    juce::String fmtPercent(double v)  { return juce::String((int)std::round(v)) + "%"; }
    juce::String fmtWidth(double v)    { return juce::String((int)std::round(v * 100.0)) + "%"; }
    juce::String fmtSignedPct(double v){ return (v >= 0 ? "+" : "") + juce::String((int)std::round(v)) + "%"; }
    juce::String fmtQ(double v)        { return juce::String(v, 2); }
    juce::String fmtMs(double v)       { return juce::String((int)std::round(v)) + " ms"; }
    juce::String fmtScale(double v)    { return juce::String(v, 2) + "x"; }
    juce::String fmtDbtp(double v)     { return juce::String(v, 1) + " dBTP"; }
    juce::String fmtOnOff(double v)    { return v >= 0.5 ? "On" : "Off"; }

    template <int N>
    ferment::FermentKnob::Formatter modeFormatter(const char* const (&names)[N])
    {
        // Captured as a plain pointer to the static table rather than as a
        // reference to the parameter, so the closure owns nothing that can go
        // out of scope underneath it.
        const char* const* table = names;

        return [table](double v)
        {
            return juce::String(table[juce::jlimit(0, N - 1, (int)std::lround(v))]);
        };
    }

    const char* const kSatNames[]  = { "Mild", "Moderate", "Hot" };
    const char* const kCharNames[] = { "Fat", "Warm", "Bright" };
    const char* const kHpNames[]   = { "Off", "100", "300" };
}

// =============================================================================
//  Header readouts
// =============================================================================

void FermentMasterEditor::ReadoutSlot::set(double sourceLufs, double sourceTp,
                                           double resultLufs, double resultTp)
{
    // Formatted into a stack buffer and compared before committing: this runs
    // twenty times a second for the life of the editor.
    char buf[48];
    bool changed = false;

    std::snprintf(buf, sizeof buf, "IN %.1f LUFS  %.1f TP", sourceLufs, sourceTp);

    if (sourceText != buf) { sourceText = buf; changed = true; }

    std::snprintf(buf, sizeof buf, "OUT %.1f LUFS  %.1f TP", resultLufs, resultTp);

    if (resultText != buf) { resultText = buf; changed = true; }

    if (changed)
        repaint();
}

void FermentMasterEditor::ReadoutSlot::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setFont(T::mono(10.0f));

    g.setColour(T::labelDim);
    g.drawText(sourceText, bounds.removeFromTop(bounds.getHeight() / 2),
               juce::Justification::centredRight, false);

    g.setColour(T::amber);
    g.drawText(resultText, bounds, juce::Justification::centredRight, false);
}

// =============================================================================
//  Editor
// =============================================================================

FermentMasterEditor::FermentMasterEditor(FermentMasterProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    addAndMakeVisible(header);
    header.setSlotComponent(&readouts, readoutSlot);

    rack.setViewedComponent(&rackContent, false);
    /*  No bar, but still scrollable: the last two arguments allow the movement
        without drawing anything for it.  The wheel works over any part of the
        rack that is not a knob — knobs consume the wheel to change their own
        value — which is card titles, backgrounds and the gaps between units.
    */
    rack.setScrollBarsShown(false, false, false, true);
    addAndMakeVisible(rack);

    // ---- STAGE: the pipeline's front end ----
    {
        auto& c = addCard(FM::ModStage, "STAGE");
        addKnob(c, Proc::kStageGain,  "GAIN",  fmtSignedDb);
        addKnob(c, Proc::kStageWidth, "WIDTH", fmtWidth);
    }

    // ---- CHARGE: density ----
    {
        auto& c = addCard(FM::ModCharge, "CHARGE");
        addKnob(c, Proc::kChargeCompression, "COMP",   fmtKnob);
        addKnob(c, Proc::kChargeAttack,      "ATTACK", fmtKnob);
        addKnob(c, Proc::kChargeRelease,     "REL",    fmtKnob);
        addKnob(c, Proc::kChargeSaturation,  "SAT",    fmtKnob);
        addKnob(c, Proc::kChargeSatMode,     "SAT MODE", modeFormatter(kSatNames));
        addKnob(c, Proc::kChargeCharacter,   "CHAR",   fmtKnob);
        addKnob(c, Proc::kChargeCharMode,    "VOICE",  modeFormatter(kCharNames));
        addKnob(c, Proc::kChargeHp,          "SC HP",  modeFormatter(kHpNames));
        addKnob(c, Proc::kChargeMix,         "MIX",    fmtPercent);

        c.meter = std::make_unique<ferment::NeedleMeter>(
            [&p] { return p.snapshot().chargeGrDb; },
            ferment::NeedleMeter::Mode::GR, ferment::NeedleMeter::Range { 0.0, 12.0 }, "GR");
        c.card->setMeterComponent(c.meter.get(), meterWidth, meterHeight);
    }

    // ---- TONE: the tilt EQ ----
    {
        auto& c = addCard(FM::ModTone, "TONE");
        addKnob(c, Proc::kToneLow,       "LOW",   fmtSignedDb);
        addKnob(c, Proc::kTonePresence,  "PRES",  fmtSignedDb);
        addKnob(c, Proc::kTonePresenceQ, "PRES Q", fmtQ);
        addKnob(c, Proc::kToneHigh,      "HIGH",  fmtSignedDb);
    }

    // ---- CLIP: shave ----
    {
        auto& c = addCard(FM::ModClip, "CLIP");
        addKnob(c, Proc::kClipCeiling, "CEILING", fmtDb1);
        addKnob(c, Proc::kClipKnee,    "KNEE",    fmtPercent);
        addKnob(c, Proc::kClipTilt,    "TILT",    fmtPercent);
        addKnob(c, Proc::kClipBias,    "BIAS",    fmtSignedPct);

        c.meter = std::make_unique<ferment::NeedleMeter>(
            [&p] { return p.snapshot().clipShaveDb; },
            ferment::NeedleMeter::Mode::GR, ferment::NeedleMeter::Range { 0.0, 6.0 }, "SHAVE");
        c.card->setMeterComponent(c.meter.get(), meterWidth, meterHeight);
    }

    // ---- LIMIT: ceiling and loudness ----
    {
        auto& c = addCard(FM::ModLimit, "LIMIT");
        addKnob(c, Proc::kLimitCeiling,  "CEILING", fmtDbtp);
        addKnob(c, Proc::kLimitAttack,   "ATTACK",  fmtMs);
        addKnob(c, Proc::kLimitRelease,  "RELEASE", fmtScale);
        addKnob(c, Proc::kLimitTruePeak, "TRUE PK", fmtOnOff);

        c.meter = std::make_unique<ferment::NeedleMeter>(
            [&p] { return p.snapshot().limitGrDb; },
            ferment::NeedleMeter::Mode::GR, ferment::NeedleMeter::Range { 0.0, 12.0 }, "GR");
        c.card->setMeterComponent(c.meter.get(), meterWidth, meterHeight);
    }

    // ---- Learn + profile ----
    addAndMakeVisible(learnButton);
    learnButton.onClick = [this]
    {
        if (learnButton.getState() != ferment::LearnButton::State::Idle)
            return;

        // Raised here and dropped on the next poll, so the audio thread sees a
        // real edge and the host records the press as automation.
        if (auto* param = processor.apvts.getParameter(Proc::paramIDs()[Proc::kLearn]))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost(1.0f);
            param->endChangeGesture();
            learnPending = true;
        }
    };

    profileBox.addItem("Studio", 1);
    profileBox.addItem("Reel", 2);
    addChildComponent(profileBox);

    for (auto* button : { &studioBtn, &reelBtn })
    {
        button->setClickingTogglesState(false);
        addAndMakeVisible(*button);
    }

    studioBtn.onClick = [this] { profileBox.setSelectedItemIndex(0, juce::sendNotificationSync); };
    reelBtn  .onClick = [this] { profileBox.setSelectedItemIndex(1, juce::sendNotificationSync); };

    profileAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, Proc::paramIDs()[Proc::kProfile], profileBox);

    profileBox.onChange = [this]
    {
        const int selected = profileBox.getSelectedItemIndex();
        studioBtn.setToggleState(selected == 0, juce::dontSendNotification);
        reelBtn  .setToggleState(selected == 1, juce::dontSendNotification);
    };
    profileBox.onChange();

    startTimerHz(pollHz);
    setSize(editorWidth, editorHeight);
}

FermentMasterEditor::~FermentMasterEditor() = default;

FermentMasterEditor::Card& FermentMasterEditor::addCard(FM::Module module,
                                                        const juce::String& title)
{
    auto entry = std::make_unique<Card>();
    entry->module = module;
    entry->card = std::make_unique<ferment::ModuleCard>(title);
    entry->bypass = std::make_unique<ferment::FermentToggle>("BYP");

    entry->bypass->attachTo(processor.apvts,
                            Proc::paramIDs()[Proc::bypassParamFor(module)]);
    entry->card->setBypassComponent(entry->bypass.get(), bypassWidth);
    entry->card->setControlWidth(controlCell);

    // Cards live inside the scrolled rack, not on the editor.
    rackContent.addAndMakeVisible(*entry->card);
    cards.push_back(std::move(entry));

    return *cards.back();
}

void FermentMasterEditor::addKnob(Card& card, Proc::Param param, const char* caption,
                                  ferment::FermentKnob::Formatter formatter)
{
    card.knobs.push_back(std::make_unique<ferment::FermentKnob>(
        processor.apvts, Proc::paramIDs()[param], caption, std::move(formatter)));

    card.card->addControl(*card.knobs.back());
}

int FermentMasterEditor::cardWidth(const Card& card) const
{
    const int chrome = ferment::ModuleCard::padding * 2
                     + (card.meter != nullptr ? meterWidth + ferment::ModuleCard::padding / 2 : 0);

    return (int)card.knobs.size() * controlCell + chrome;
}

void FermentMasterEditor::timerCallback()
{
    if (learnPending)
    {
        if (auto* param = processor.apvts.getParameter(Proc::paramIDs()[Proc::kLearn]))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost(0.0f);
            param->endChangeGesture();
        }

        learnPending = false;
    }

    const auto snap = processor.snapshot();
    readouts.set(snap.sourceLufs, snap.sourceTruePeak, snap.resultLufs, snap.resultTruePeak);

    for (auto& card : cards)
    {
        auto* param = processor.apvts.getRawParameterValue(
            Proc::paramIDs()[Proc::bypassParamFor(card->module)]);

        card->card->setBypassed(param != nullptr && param->load() > 0.5f);
    }

    /*  The face draws the Learn the processor is running; it does not drive it.
        The write-back is the processor's own business precisely so a Learn with
        this window closed still lands on the knobs.
    */
    const auto status = processor.learnStatus();

    if (status.phase == 1)
        learnButton.setState(ferment::LearnButton::State::Listening, status.progress);
    else if (status.phase == 2)
        learnButton.setState(ferment::LearnButton::State::Setting);
    else if (lastLearnPhase == 2)
        learnButton.setState(ferment::LearnButton::State::Done);
    else if (lastLearnPhase == 1)
        learnButton.setState(ferment::LearnButton::State::Idle);  // cancelled

    lastLearnPhase = status.phase;
}

void FermentMasterEditor::paint(juce::Graphics& g)
{
    /*  Three layers, all from the existing palette, so it is obvious what is the
        host and what is the plugin: the page black around the edge, the rack
        well Master itself occupies, and — painted by the cards — the ordinary
        chassis face each module wears.  Master gets its own colour precisely so
        the modules do not have to change theirs.
    */
    g.fillAll(T::bg);

    /*  woodShadow, the family's warm dark, for the rack body.  faceEdge sits
        between faceTop and faceBottom, so a well painted in it disappeared
        against the very card faces it was supposed to frame.
    */
    g.setColour(T::woodShadow);
    g.fillRect(rackWell);

    g.setColour(T::bg.withAlpha(0.5f));
    g.drawRect(rackWell, 1);
}

void FermentMasterEditor::resized()
{
    auto inner = ferment::ChassisPanel::contentArea(getLocalBounds());

    header.setBounds(inner.removeFromTop(ferment::ChassisPanel::headerHeight));
    inner.removeFromTop(ferment::ChassisPanel::headerGap);

    // ---- the rack: one horizontal run of module faces, scrolled ----
    rackWell = inner.removeFromTop(rackPad * 2 + cardHeight);
    rack.setBounds(rackWell);

    int x = rackPad;

    for (auto& card : cards)
    {
        const int width = cardWidth(*card);
        card->card->setBounds(x, rackPad, width, cardHeight);
        x += width + cardGap;
    }

    // The content is as wide as the chain needs; the viewport scrolls it.  The
    // trailing pad matches the leading one, so the far end of the rack looks
    // like the near end rather than like a card that ran out of room.
    rackContent.setSize(juce::jmax(rack.getMaximumVisibleWidth(),
                                   x - cardGap + rackPad),
                        rack.getMaximumVisibleHeight());

    inner.removeFromTop(bottomGap);

    auto bar = inner.removeFromTop(bottomBar);

    constexpr int profileButton = 70;
    constexpr int learnWidth    = 220;

    // Flush left, so the controls line up with the rack above them rather than
    // floating in the middle of a face whose contents start at the left edge.
    studioBtn.setBounds(bar.removeFromLeft(profileButton).reduced(1, 3));
    reelBtn.setBounds(bar.removeFromLeft(profileButton).reduced(1, 3));
    bar.removeFromLeft(16);
    learnButton.setBounds(bar.removeFromLeft(learnWidth).reduced(1, 3));
}

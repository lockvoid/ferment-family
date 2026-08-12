#include "FermentUtilityEditor.h"
#include "FermentUtilityProcessor.h"

#include <cmath>

namespace T = ferment::theme;

// =============================================================================
//  Toggle styling
// =============================================================================

FermentUtilityEditor::ToggleLookAndFeel::ToggleLookAndFeel()
{
    setColour(juce::TextButton::textColourOffId, T::labelDim);
    setColour(juce::TextButton::textColourOnId,  T::faceEdge);
    setColour(juce::PopupMenu::backgroundColourId, T::faceTop);
    setColour(juce::PopupMenu::textColourId,       T::labelCream);
}

void FermentUtilityEditor::ToggleLookAndFeel::drawButtonBackground(
    juce::Graphics& g, juce::Button& button, const juce::Colour&,
    bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    const bool on = button.getToggleState();

    auto fill = on ? T::amber : T::faceEdge;
    if (isDown || isHighlighted)
        fill = fill.brighter(0.08f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 3.0f);

    // Lit buttons carry no outline — the amber fill is the state, and a darker
    // rim around it only softens the edge that is doing the talking.
    if (!on)
    {
        g.setColour(T::woodDark);
        g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
    }
}

juce::Font FermentUtilityEditor::ToggleLookAndFeel::getTextButtonFont(juce::TextButton&, int)
{
    return T::monoTracked(10.0f, true);
}

// =============================================================================
//  Editor
// =============================================================================

FermentUtilityEditor::FermentUtilityEditor(FermentUtilityProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    addAndMakeVisible(chassis);
    addAndMakeVisible(header);

    // ---- Continuous knobs ----
    addKnob("gain", "GAIN", [](double n) {
        return juce::String(FermentUtilityProcessor::gainFromNorm(n), 1) + " dB";
    });
    addKnob("balL", "BAL L", [](double n) {
        return juce::String((int)std::round(FermentUtilityProcessor::balanceFromNorm(n) * 100.0));
    });
    addKnob("balR", "BAL R", [](double n) {
        return juce::String((int)std::round(FermentUtilityProcessor::balanceFromNorm(n) * 100.0));
    });
    addKnob("width", "WIDTH", [](double n) {
        return juce::String((int)std::round(FermentUtilityProcessor::widthFromNorm(n) * 100.0)) + "%";
    });
    addKnob("bassmonofreq", "BASS F", [](double n) {
        return juce::String((int)std::round(FermentUtilityProcessor::bassMonoFromNorm(n))) + " Hz";
    });

    // ---- Toggles ----
    for (auto* b : { &muteBtn, &phaseLBtn, &phaseRBtn, &dcBtn, &bassMonoBtn })
        setupToggle(*b, true);

    muteAtt      = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, "mute",      muteBtn);
    phaseLAtt    = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, "phaseL",    phaseLBtn);
    phaseRAtt    = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, "phaseR",    phaseRBtn);
    dcAtt        = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, "dc",        dcBtn);
    bassMonoAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, "bassmono",  bassMonoBtn);

    // ---- Channel mode (5-button bar, driven by hidden ComboBox so we can attach) ----
    chanHidden.addItem("Stereo", 1);
    chanHidden.addItem("Swap",   2);
    chanHidden.addItem("Left",   3);
    chanHidden.addItem("Right",  4);
    chanHidden.addItem("Mono",   5);
    addChildComponent(chanHidden); // not visible — we drive it via buttons

    const char* labels[] = { "ST", "SW", "L", "R", "M" };
    for (int i = 0; i < 5; ++i)
    {
        chanBtns[(size_t)i].setButtonText(labels[i]);
        chanBtns[(size_t)i].onClick = [this, i]() { chanHidden.setSelectedItemIndex(i, juce::sendNotificationSync); };
        setupToggle(chanBtns[(size_t)i], false);
    }
    chanAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, "chanmode", chanHidden);
    chanHidden.onChange = [this]() {
        const int sel = chanHidden.getSelectedItemIndex();
        for (int i = 0; i < (int)chanBtns.size(); ++i)
            chanBtns[(size_t)i].setToggleState(i == sel, juce::dontSendNotification);
    };
    chanHidden.onChange();

    // ---- Mid/Side solo bar (Off / Mid / Side) ----
    msHidden.addItem("Off",  1);
    msHidden.addItem("Mid",  2);
    msHidden.addItem("Side", 3);
    addChildComponent(msHidden);

    const char* msLabels[] = { "OFF", "MID", "SIDE" };
    for (int i = 0; i < 3; ++i)
    {
        msBtns[(size_t)i].setButtonText(msLabels[i]);
        msBtns[(size_t)i].onClick = [this, i]() { msHidden.setSelectedItemIndex(i, juce::sendNotificationSync); };
        setupToggle(msBtns[(size_t)i], false);
    }
    msAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, "mssolo", msHidden);
    msHidden.onChange = [this]() {
        const int sel = msHidden.getSelectedItemIndex();
        for (int i = 0; i < (int)msBtns.size(); ++i)
            msBtns[(size_t)i].setToggleState(i == sel, juce::dontSendNotification);
    };
    msHidden.onChange();

    setSize(660, 204);
}

FermentUtilityEditor::~FermentUtilityEditor()
{
    // The toggles hold a raw pointer to a member look-and-feel; drop it before
    // either goes out of scope.
    for (auto* b : { &muteBtn, &phaseLBtn, &phaseRBtn, &dcBtn, &bassMonoBtn })
        b->setLookAndFeel(nullptr);

    for (auto& b : chanBtns) b.setLookAndFeel(nullptr);
    for (auto& b : msBtns)   b.setLookAndFeel(nullptr);
}

void FermentUtilityEditor::addKnob(const char* paramId, const char* caption,
                                   ferment::FermentKnob::Formatter formatter)
{
    knobs.push_back(std::make_unique<ferment::FermentKnob>(
        processor.apvts, paramId, caption, std::move(formatter)));
    addAndMakeVisible(*knobs.back());
}

void FermentUtilityEditor::setupToggle(juce::TextButton& b, bool clickToggles)
{
    b.setClickingTogglesState(clickToggles);
    b.setLookAndFeel(&toggleLnf);
    addAndMakeVisible(b);
}

void FermentUtilityEditor::paint(juce::Graphics& g)
{
    g.fillAll(T::bg);
}

void FermentUtilityEditor::resized()
{
    auto inner = ferment::ChassisPanel::layoutFrame(getLocalBounds(), chassis, header);

    // Bottom bar: Channel Mode | M/S Solo | DC + Bass Mono
    auto barArea = inner.removeFromBottom(30);
    inner.removeFromBottom(10);

    {
        auto chanArea = barArea.removeFromLeft(barArea.getWidth() * 4 / 10);
        auto msArea   = barArea.removeFromLeft(barArea.getWidth() * 3 / 6);
        auto filtArea = barArea;

        const int chanW = chanArea.getWidth() / 5;
        for (int i = 0; i < 5; ++i)
            chanBtns[(size_t)i].setBounds(chanArea.removeFromLeft(chanW).reduced(2));

        msArea.removeFromLeft(8);
        const int msW = msArea.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
            msBtns[(size_t)i].setBounds(msArea.removeFromLeft(msW).reduced(2));

        filtArea.removeFromLeft(8);
        const int filtW = filtArea.getWidth() / 2;
        dcBtn.setBounds(filtArea.removeFromLeft(filtW).reduced(2));
        bassMonoBtn.setBounds(filtArea.removeFromLeft(filtW).reduced(2));
    }

    // Knob row, with the phase/mute column alongside it.  The knobs and the
    // button column are centred as one block so the row stays balanced.
    using Knob = ferment::FermentKnob;

    const int face      = Knob::faceSizeFor(inner, (int)knobs.size() + 1, 1);
    const int cellW     = face + Knob::gutter;
    const int buttonsW  = 96;
    const int blockW    = cellW * (int)knobs.size() + Knob::gutter + buttonsW;

    auto block = inner.withSizeKeepingCentre(juce::jmin(inner.getWidth(), blockW),
                                             inner.getHeight());

    Knob::layoutGrid(block.removeFromLeft(cellW * (int)knobs.size()), knobs,
                     (int)knobs.size());
    block.removeFromLeft(Knob::gutter);

    {
        auto col = block.removeFromLeft(buttonsW)
                        .withSizeKeepingCentre(buttonsW, 3 * 26 + 12);

        phaseLBtn.setBounds(col.removeFromTop(26));
        col.removeFromTop(4);
        phaseRBtn.setBounds(col.removeFromTop(26));
        col.removeFromTop(8);
        muteBtn.setBounds(col.removeFromTop(26));
    }
}

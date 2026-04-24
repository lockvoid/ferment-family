#include "FermentUtilityEditor.h"
#include "FermentUtilityProcessor.h"
#include "../common/WarmLookAndFeel.h"
#include "../common/WarmPalette.h"

#include <cmath>

namespace P = ferment::palette;

FermentUtilityEditor::FermentUtilityEditor(FermentUtilityProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    lnf = std::make_unique<WarmLookAndFeel>();
    setLookAndFeel(lnf.get());

    // ---- Gain knob ----
    gain.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    gain.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    gain.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                              juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible(gain);

    gainName.setText("GAIN", juce::dontSendNotification);
    gainName.setJustificationType(juce::Justification::centred);
    gainName.setColour(juce::Label::textColourId, P::labelDim);
    gainName.setFont(juce::Font(10.0f, juce::Font::bold));
    addAndMakeVisible(gainName);

    gainValue.setJustificationType(juce::Justification::centred);
    gainValue.setColour(juce::Label::textColourId, P::amber);
    gainValue.setFont(juce::Font(11.0f));
    addAndMakeVisible(gainValue);

    gain.onValueChange = [this]() {
        const double dB = FermentUtilityProcessor::gainFromNorm(gain.getValue());
        gainValue.setText(juce::String(dB, 1) + " dB", juce::dontSendNotification);
    };
    gainAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "gain", gain);
    gain.onValueChange();

    // ---- Toggles ----
    auto toggle = [this](juce::TextButton& b, const char*) {
        b.setClickingTogglesState(true);
        addAndMakeVisible(b);
    };
    toggle(muteBtn,   "mute");
    toggle(phaseLBtn, "phaseL");
    toggle(phaseRBtn, "phaseR");
    muteAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, "mute",   muteBtn);
    phaseLAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, "phaseL", phaseLBtn);
    phaseRAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, "phaseR", phaseRBtn);

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
        chanBtns[i].setButtonText(labels[i]);
        chanBtns[i].setClickingTogglesState(false);
        chanBtns[i].onClick = [this, i]() { chanHidden.setSelectedItemIndex(i, juce::sendNotificationSync); };
        addAndMakeVisible(chanBtns[i]);
    }
    chanAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, "chanmode", chanHidden);
    chanHidden.onChange = [this]() {
        const int sel = chanHidden.getSelectedItemIndex();
        for (int i = 0; i < (int)chanBtns.size(); ++i)
            chanBtns[i].setToggleState(i == sel, juce::dontSendNotification);
    };
    chanHidden.onChange();

    setSize(640, 260);
}

FermentUtilityEditor::~FermentUtilityEditor() { setLookAndFeel(nullptr); }

void FermentUtilityEditor::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    juce::ColourGradient bg(P::panel, b.getCentreX(), b.getCentreY() - 30.f,
                            P::chassis, b.getCentreX(), b.getHeight(), true);
    g.setGradientFill(bg);
    g.fillAll();
    g.setColour(P::chassisEdge);
    g.drawRect(b, 2.0f);
    g.setColour(P::panelHi.withAlpha(0.6f));
    g.drawRect(b.reduced(2.0f), 1.0f);

    auto titleArea = b.removeFromTop(50.0f).reduced(14.0f, 10.0f);
    g.setColour(P::separator);
    g.drawLine(titleArea.getX(), titleArea.getBottom() + 6.0f,
               titleArea.getRight(), titleArea.getBottom() + 6.0f, 1.0f);

    g.setColour(P::labelCream);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::bold));
    g.drawText("FERMENT", titleArea.removeFromLeft(100), juce::Justification::left, false);

    g.setColour(P::amber);
    g.setFont(juce::Font(14.0f, juce::Font::plain));
    g.drawText("Utility  /  routing + gain", titleArea, juce::Justification::right, false);

    // Section labels above clusters
    g.setColour(P::labelDim);
    g.setFont(juce::Font(9.0f, juce::Font::bold));
}

void FermentUtilityEditor::resized()
{
    auto area = getLocalBounds().withTrimmedTop(60).reduced(16, 12);

    // Left column: Gain knob
    auto gainCol = area.removeFromLeft(120);
    gainName.setBounds(gainCol.removeFromTop(14));
    auto gainKnobArea = gainCol.removeFromTop(80).withSizeKeepingCentre(72, 72);
    gain.setBounds(gainKnobArea);
    gainValue.setBounds(gainCol.removeFromTop(18));

    area.removeFromLeft(8);

    // Middle: toggles stacked (Phase L / Phase R / Mute)
    auto toggleCol = area.removeFromLeft(80);
    const int btnH = 30;
    phaseLBtn.setBounds(toggleCol.removeFromTop(btnH));
    toggleCol.removeFromTop(4);
    phaseRBtn.setBounds(toggleCol.removeFromTop(btnH));
    toggleCol.removeFromTop(8);
    muteBtn.setBounds(toggleCol.removeFromTop(btnH));

    area.removeFromLeft(12);

    // Right: channel mode bar
    auto chanArea = area.removeFromTop(40);
    const int btnW = chanArea.getWidth() / 5;
    for (int i = 0; i < 5; ++i)
        chanBtns[i].setBounds(chanArea.removeFromLeft(btnW).reduced(2));
}

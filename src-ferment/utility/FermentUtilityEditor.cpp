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

    // ---- Continuous knobs ----
    setupKnob(gainK, "gain", "GAIN", [](double n) {
        return juce::String(FermentUtilityProcessor::gainFromNorm(n), 1) + " dB";
    });
    setupKnob(balLK, "balL", "BAL L", [](double n) {
        const double b = FermentUtilityProcessor::balanceFromNorm(n);
        return juce::String((int)std::round(b * 100.0));
    });
    setupKnob(balRK, "balR", "BAL R", [](double n) {
        const double b = FermentUtilityProcessor::balanceFromNorm(n);
        return juce::String((int)std::round(b * 100.0));
    });
    setupKnob(widthK, "width", "WIDTH", [](double n) {
        return juce::String((int)std::round(FermentUtilityProcessor::widthFromNorm(n) * 100.0)) + "%";
    });
    setupKnob(bassMonoFreqK, "bassmonofreq", "BASS F", [](double n) {
        const double f = FermentUtilityProcessor::bassMonoFromNorm(n);
        return juce::String((int)std::round(f)) + " Hz";
    });

    // ---- Toggles ----
    auto toggle = [this](juce::TextButton& b, const char*) {
        b.setClickingTogglesState(true);
        addAndMakeVisible(b);
    };
    toggle(muteBtn,      "mute");
    toggle(phaseLBtn,    "phaseL");
    toggle(phaseRBtn,    "phaseR");
    toggle(dcBtn,        "dc");
    toggle(bassMonoBtn,  "bassmono");
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

    // ---- Mid/Side solo bar (Off / Mid / Side) ----
    msHidden.addItem("Off",  1);
    msHidden.addItem("Mid",  2);
    msHidden.addItem("Side", 3);
    addChildComponent(msHidden);
    const char* msLabels[] = { "OFF", "MID", "SIDE" };
    for (int i = 0; i < 3; ++i)
    {
        msBtns[i].setButtonText(msLabels[i]);
        msBtns[i].setClickingTogglesState(false);
        msBtns[i].onClick = [this, i]() { msHidden.setSelectedItemIndex(i, juce::sendNotificationSync); };
        addAndMakeVisible(msBtns[i]);
    }
    msAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, "mssolo", msHidden);
    msHidden.onChange = [this]() {
        const int sel = msHidden.getSelectedItemIndex();
        for (int i = 0; i < (int)msBtns.size(); ++i)
            msBtns[i].setToggleState(i == sel, juce::dontSendNotification);
    };
    msHidden.onChange();

    setSize(860, 300);
}

FermentUtilityEditor::~FermentUtilityEditor() { setLookAndFeel(nullptr); }

void FermentUtilityEditor::setupKnob(Knob& k, const char* paramId, const char* name,
                                      std::function<juce::String(double)> fmt)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    k.slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                  juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible(k.slider);

    k.name.setText(name, juce::dontSendNotification);
    k.name.setJustificationType(juce::Justification::centred);
    k.name.setColour(juce::Label::textColourId, P::labelDim);
    k.name.setFont(juce::Font(10.0f, juce::Font::bold));
    addAndMakeVisible(k.name);

    k.value.setJustificationType(juce::Justification::centred);
    k.value.setColour(juce::Label::textColourId, P::amber);
    k.value.setFont(juce::Font(11.0f));
    addAndMakeVisible(k.value);

    auto update = [&k, fmt]() { k.value.setText(fmt(k.slider.getValue()), juce::dontSendNotification); };
    k.slider.onValueChange = update;
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, paramId, k.slider);
    update();
}

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
    auto area = getLocalBounds().withTrimmedTop(60).reduced(14, 10);

    // Row of knobs (Gain / Bal L / Bal R / Width / Bass Mono Freq)
    auto knobRow = area.removeFromTop(120);
    Knob* ks[] = { &gainK, &balLK, &balRK, &widthK, &bassMonoFreqK };
    const int cellW = knobRow.getWidth() / 6;  // 5 knobs + 1 cell for toggles
    for (int i = 0; i < 5; ++i)
    {
        auto cell = knobRow.removeFromLeft(cellW);
        ks[i]->name.setBounds(cell.removeFromTop(14));
        ks[i]->value.setBounds(cell.removeFromBottom(16));
        ks[i]->slider.setBounds(cell.withSizeKeepingCentre(68, 68));
    }

    // Right cell: stacked toggles (Phase L / Phase R / Mute)
    {
        auto col = knobRow;
        const int btnH = 24;
        phaseLBtn.setBounds(col.removeFromTop(btnH).reduced(4, 2));
        col.removeFromTop(2);
        phaseRBtn.setBounds(col.removeFromTop(btnH).reduced(4, 2));
        col.removeFromTop(6);
        muteBtn.setBounds(col.removeFromTop(btnH).reduced(4, 2));
    }

    area.removeFromTop(6);

    // Bottom bar: Channel Mode | M/S Solo | DC + Bass Mono
    auto barArea = area.removeFromTop(38);
    auto chanArea = barArea.removeFromLeft(barArea.getWidth() * 4 / 10);
    auto msArea   = barArea.removeFromLeft(barArea.getWidth() * 3 / 6);
    auto filtArea = barArea;

    {
        const int btnW = chanArea.getWidth() / 5;
        for (int i = 0; i < 5; ++i)
            chanBtns[i].setBounds(chanArea.removeFromLeft(btnW).reduced(2));
    }
    {
        msArea.removeFromLeft(8);
        const int btnW = msArea.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
            msBtns[i].setBounds(msArea.removeFromLeft(btnW).reduced(2));
    }
    {
        filtArea.removeFromLeft(8);
        const int btnW = filtArea.getWidth() / 2;
        dcBtn.setBounds(filtArea.removeFromLeft(btnW).reduced(2));
        bassMonoBtn.setBounds(filtArea.removeFromLeft(btnW).reduced(2));
    }
}

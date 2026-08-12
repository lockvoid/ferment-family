#include "FermentUtilityEditor.h"
#include "FermentUtilityProcessor.h"

#include <cmath>

namespace T = ferment::theme;

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
    // attachTo() sets the clicking-toggles-state for us and keeps the
    // attachment inside the button, where it cannot outlive its control.
    const std::pair<ferment::FermentToggle*, const char*> boolToggles[] = {
        { &muteBtn,     "mute"     },
        { &phaseLBtn,   "phaseL"   },
        { &phaseRBtn,   "phaseR"   },
        { &dcBtn,       "dc"       },
        { &bassMonoBtn, "bassmono" },
    };

    for (auto& [button, paramId] : boolToggles)
    {
        addAndMakeVisible(*button);
        button->attachTo(processor.apvts, paramId);
    }

    // ---- Channel mode (5-button bar, driven by hidden ComboBox so we can attach) ----
    chanHidden.addItem("Stereo", 1);
    chanHidden.addItem("Swap",   2);
    chanHidden.addItem("Left",   3);
    chanHidden.addItem("Right",  4);
    chanHidden.addItem("Mono",   5);
    addChildComponent(chanHidden); // not visible — we drive it via buttons

    // Bar buttons drive the hidden ComboBox from onClick, so their toggle state
    // is set for them by its onChange rather than by the click — which is
    // juce::Button's default, so there is nothing to turn off.
    const char* labels[] = { "ST", "SW", "L", "R", "M" };
    for (int i = 0; i < 5; ++i)
    {
        chanBtns[(size_t)i].setButtonText(labels[i]);
        chanBtns[(size_t)i].onClick = [this, i]() { chanHidden.setSelectedItemIndex(i, juce::sendNotificationSync); };
        addAndMakeVisible(chanBtns[(size_t)i]);
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
        addAndMakeVisible(msBtns[(size_t)i]);
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

FermentUtilityEditor::~FermentUtilityEditor() = default;

void FermentUtilityEditor::addKnob(const char* paramId, const char* caption,
                                   ferment::FermentKnob::Formatter formatter)
{
    knobs.push_back(std::make_unique<ferment::FermentKnob>(
        processor.apvts, paramId, caption, std::move(formatter)));
    addAndMakeVisible(*knobs.back());
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
        constexpr int btnH = ferment::FermentToggle::standardHeight;
        auto col = block.removeFromLeft(buttonsW)
                        .withSizeKeepingCentre(buttonsW, 3 * btnH + 12);

        phaseLBtn.setBounds(col.removeFromTop(btnH));
        col.removeFromTop(4);
        phaseRBtn.setBounds(col.removeFromTop(btnH));
        col.removeFromTop(8);
        muteBtn.setBounds(col.removeFromTop(btnH));
    }
}

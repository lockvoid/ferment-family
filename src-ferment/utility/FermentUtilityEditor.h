#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class FermentUtilityProcessor;
class WarmLookAndFeel;

class FermentUtilityEditor : public juce::AudioProcessorEditor
{
public:
    explicit FermentUtilityEditor(FermentUtilityProcessor&);
    ~FermentUtilityEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    FermentUtilityProcessor& processor;
    std::unique_ptr<WarmLookAndFeel> lnf;

    // Gain rotary + value
    juce::Slider gain;
    juce::Label  gainName, gainValue;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAtt;

    // Boolean toggles
    juce::TextButton muteBtn   { "MUTE"   };
    juce::TextButton phaseLBtn { "\u00F8 L" };
    juce::TextButton phaseRBtn { "\u00F8 R" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtt, phaseLAtt, phaseRAtt;

    // Channel mode as button bar (Stereo / Swap / L / R / Mono)
    std::array<juce::TextButton, 5> chanBtns;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chanAtt;
    juce::ComboBox chanHidden; // attached to APVTS; buttons sync with it

    void wireChanButtons();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentUtilityEditor)
};

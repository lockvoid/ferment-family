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

    // Continuous rotaries
    struct Knob {
        juce::Slider slider;
        juce::Label  name, value;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    Knob gainK, balLK, balRK, widthK;

    void setupKnob(Knob& k, const char* paramId, const char* name,
                   std::function<juce::String(double)> fmt);

    // Boolean toggles
    juce::TextButton muteBtn   { "MUTE"   };
    juce::TextButton phaseLBtn { "\u00F8 L" };
    juce::TextButton phaseRBtn { "\u00F8 R" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtt, phaseLAtt, phaseRAtt;

    // Channel mode as button bar (Stereo / Swap / L / R / Mono)
    std::array<juce::TextButton, 5> chanBtns;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chanAtt;
    juce::ComboBox chanHidden;

    // Mid/Side solo as 3-button bar (Off / Mid / Side)
    std::array<juce::TextButton, 3> msBtns;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> msAtt;
    juce::ComboBox msHidden;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentUtilityEditor)
};

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class FermentGlueProcessor;

class FermentGlueEditor : public juce::AudioProcessorEditor
{
public:
    explicit FermentGlueEditor(FermentGlueProcessor&);
    ~FermentGlueEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    FermentGlueProcessor& processor;

    class WarmLookAndFeel;
    std::unique_ptr<WarmLookAndFeel> lnf;

    struct KnobWithLabel
    {
        juce::Slider slider;
        juce::Label  name;
        juce::Label  value;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    std::array<KnobWithLabel, 10> knobs;

    void setupKnob(KnobWithLabel& k, const char* paramID, const char* displayName,
                   std::function<juce::String(double)> displayFn);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentGlueEditor)
};

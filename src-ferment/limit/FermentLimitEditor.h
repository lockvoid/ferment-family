#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>

class FermentLimitProcessor;
class WarmLookAndFeel;

class FermentLimitEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit FermentLimitEditor(FermentLimitProcessor&);
    ~FermentLimitEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    FermentLimitProcessor& processor;

    std::unique_ptr<WarmLookAndFeel> lnf;

    struct KnobWithLabel
    {
        juce::Slider slider;
        juce::Label  name;
        juce::Label  value;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    std::array<KnobWithLabel, 8> knobs;
    juce::Label grMeter;
    double grHold = 0.0;

    void setupKnob(KnobWithLabel& k, const char* paramID, const char* displayName,
                   std::function<juce::String(double)> displayFn);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentLimitEditor)
};

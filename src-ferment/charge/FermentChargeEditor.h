#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>

class FermentChargeProcessor;
class WarmLookAndFeel;

class FermentChargeEditor : public juce::AudioProcessorEditor
{
public:
    explicit FermentChargeEditor(FermentChargeProcessor&);
    ~FermentChargeEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    FermentChargeProcessor& processor;

    std::unique_ptr<WarmLookAndFeel> lnf;

    struct KnobWithLabel
    {
        juce::Slider slider;
        juce::Label  name;
        juce::Label  value;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    std::array<KnobWithLabel, 14> knobs;

    void setupKnob(KnobWithLabel& k, const char* paramID, const char* displayName,
                   std::function<juce::String(double)> displayFn);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentChargeEditor)
};

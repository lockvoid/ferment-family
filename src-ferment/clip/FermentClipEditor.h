#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>

class FermentClipProcessor;
class WarmLookAndFeel;

class FermentClipEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit FermentClipEditor(FermentClipProcessor&);
    ~FermentClipEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    FermentClipProcessor& processor;

    std::unique_ptr<WarmLookAndFeel> lnf;

    struct KnobWithLabel
    {
        juce::Slider slider;
        juce::Label  name;
        juce::Label  value;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    std::array<KnobWithLabel, 9> knobs;
    juce::Label shaveMeter;
    double shaveHold = 0.0;

    void setupKnob(KnobWithLabel& k, const char* paramID, const char* displayName,
                   std::function<juce::String(double)> displayFn);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentClipEditor)
};

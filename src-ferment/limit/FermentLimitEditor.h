#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <ferment_ui/ferment_ui.h>

#include <memory>
#include <vector>

class FermentLimitProcessor;

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

    void addKnob(const char* paramID, const char* caption,
                 ferment::FermentKnob::Formatter formatter);

    FermentLimitProcessor& processor;

    ferment::ChassisPanel chassis;
    ferment::HeaderBar    header { "LIMIT / DUAL-STAGE TRUE-PEAK LIMITER" };

    std::vector<std::unique_ptr<ferment::FermentKnob>> knobs;

    juce::Label grMeter;
    double grHold = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentLimitEditor)
};

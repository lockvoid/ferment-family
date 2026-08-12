#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <ferment_ui/ferment_ui.h>

#include <memory>
#include <vector>

class FermentChargeProcessor;

class FermentChargeEditor : public juce::AudioProcessorEditor
{
public:
    explicit FermentChargeEditor(FermentChargeProcessor&);
    ~FermentChargeEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void addKnob(const char* paramID, const char* caption,
                 ferment::FermentKnob::Formatter formatter);

    FermentChargeProcessor& processor;

    ferment::ChassisPanel chassis;
    ferment::HeaderBar    header { "CHARGE / HIGH OCTANE LEVELLING COMPRESSOR" };
    ferment::NeedleMeter  grMeter;

    std::vector<std::unique_ptr<ferment::FermentKnob>> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentChargeEditor)
};

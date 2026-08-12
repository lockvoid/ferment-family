#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <ferment_ui/ferment_ui.h>

#include <memory>
#include <vector>

class FermentGlueProcessor;

class FermentGlueEditor : public juce::AudioProcessorEditor
{
public:
    explicit FermentGlueEditor(FermentGlueProcessor&);
    ~FermentGlueEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void addKnob(const char* paramID, const char* caption,
                 ferment::FermentKnob::Formatter formatter);

    FermentGlueProcessor& processor;

    ferment::ChassisPanel chassis;
    ferment::HeaderBar    header { "GLUE / SSL-STYLE BUS COMP" };
    ferment::NeedleMeter  grMeter;

    std::vector<std::unique_ptr<ferment::FermentKnob>> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentGlueEditor)
};

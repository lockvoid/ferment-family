#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <ferment_ui/ferment_ui.h>

#include <memory>
#include <vector>

class FermentClipProcessor;

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

    void addKnob(const char* paramID, const char* caption,
                 ferment::FermentKnob::Formatter formatter);

    FermentClipProcessor& processor;

    ferment::ChassisPanel chassis;
    ferment::HeaderBar    header { "CLIP / ADAA MASTERING CLIPPER" };

    std::vector<std::unique_ptr<ferment::FermentKnob>> knobs;

    juce::Label shaveMeter;
    double shaveHold = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentClipEditor)
};

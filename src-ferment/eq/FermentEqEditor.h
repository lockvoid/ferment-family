#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <ferment_ui/ferment_ui.h>

#include <memory>
#include <vector>

class FermentEqProcessor;

class FermentEqEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit FermentEqEditor(FermentEqProcessor&);
    ~FermentEqEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    /** Rebuilds the FREQ / GAIN / Q row so it addresses the selected band.
        Each FermentKnob owns its attachment for one parameter ID, so following
        the selection means new knobs rather than re-pointed ones. */
    void showBand(int band);

    /** The band caption names a type and an on/off state that the graph's
        right-click menu can change without the selection moving, so it is
        refreshed on a timer rather than written once per selection. */
    void refreshBandLabel();
    void timerCallback() override;

    FermentEqProcessor& processor;

    ferment::ChassisPanel chassis;
    ferment::HeaderBar    header { "EQ / 8-BAND PARAMETRIC" };
    ferment::EqGraphEditor graph;

    juce::Label bandLabel;

    std::vector<std::unique_ptr<ferment::FermentKnob>> bandKnobs;
    std::unique_ptr<ferment::FermentKnob> outputKnob;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentEqEditor)
};

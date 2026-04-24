#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class FermentEqProcessor;
class WarmLookAndFeel;

class FermentEqEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FermentEqEditor(FermentEqProcessor&);
    ~FermentEqEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    FermentEqProcessor& processor;
    std::unique_ptr<WarmLookAndFeel> lnf;

    struct BandRow
    {
        juce::Label         numLabel;
        juce::ComboBox      typeBox;
        juce::Slider        freq, gain, q;
        juce::Label         freqVal, gainVal, qVal;
        juce::TextButton    onBtn { "ON" };

        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>   typeAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>     freqAtt, gainAtt, qAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>     onAtt;
    };

    std::array<BandRow, 8> bandRows;

    juce::Slider       outputKnob;
    juce::Label        outputName, outputVal;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAtt;

    juce::Rectangle<int> graphArea;
    void paintResponse(juce::Graphics&);

    void setupBand(int b);

    // Timer: repaint the graph at modest rate so it tracks user input.
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentEqEditor)
};

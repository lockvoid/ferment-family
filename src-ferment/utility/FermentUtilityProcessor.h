#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Ferment Utility — clone of Ableton's Utility plugin.
// Routing, gain, phase, stereo field, DC filter, bass mono.
class FermentUtilityProcessor : public juce::AudioProcessor
{
public:
    FermentUtilityProcessor();
    ~FermentUtilityProcessor() override = default;

    enum ChannelMode { Stereo = 0, Swap, LeftOnly, RightOnly, Mono, kNumModes };

    static const char* channelModeName(int m);

    // ---- ranges / display helpers ----
    static double gainFromNorm(double v)   { return (v - 0.5) * 70.0; }       // -35 .. +35 dB
    static double gainToNorm (double dB)   { return (dB / 70.0) + 0.5; }

    // ---- AudioProcessor ----
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;

    void processBlock(juce::AudioBuffer<float>&,  juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Ferment Utility"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout buildLayout();

    template <typename T>
    void processBlockT(juce::AudioBuffer<T>& buffer);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentUtilityProcessor)
};

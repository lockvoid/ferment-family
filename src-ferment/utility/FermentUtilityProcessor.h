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
    enum MSSolo      { SoloOff = 0, MidOnly, SideOnly, kNumSolos };

    static const char* channelModeName(int m);
    static const char* msSoloName(int m);

    // ---- ranges / display helpers ----
    static double gainFromNorm(double v)    { return (v - 0.5) * 70.0; }         // -35 .. +35 dB
    static double gainToNorm (double dB)    { return (dB / 70.0) + 0.5; }
    static double widthFromNorm(double v)   { return v * 4.0; }                  // 0 .. 400%
    static double balanceFromNorm(double v) { return (v - 0.5) * 2.0; }          // -1 .. +1
    static double bassMonoFromNorm(double v){ return 40.0 * std::pow(25.0, v); } // 40 .. 1000 Hz log

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

    // Filter state
    double sampleRate    { 48000.0 };
    // DC filter: one-pole HP at ~5 Hz, per-channel state
    double dcPrevInL     { 0.0 }, dcPrevOutL { 0.0 };
    double dcPrevInR     { 0.0 }, dcPrevOutR { 0.0 };
    // Bass-mono crossover: 2nd-order Butterworth LP, per channel.
    // High band is reconstructed as (input − low), which gives perfect sum.
    struct LpState { double z1 = 0.0, z2 = 0.0; };
    LpState bmLpL, bmLpR;
    double  bmB0 = 1.0, bmB1 = 0.0, bmB2 = 0.0, bmA1 = 0.0, bmA2 = 0.0;
    double  bmLastFreq { -1.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentUtilityProcessor)
};

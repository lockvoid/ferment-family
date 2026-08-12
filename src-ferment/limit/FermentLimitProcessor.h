#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace airwinconsolidated::FermentLimit { class FermentLimit; }

// Standalone JUCE plugin wrapping the Ferment-owned FermentLimit DSP.
class FermentLimitProcessor : public juce::AudioProcessor
{
public:
    FermentLimitProcessor();
    ~FermentLimitProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Ferment Limit"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Ordered to match FermentLimit::kParam*.
    enum { kGain, kCeiling, kAttack, kRelease, kTransLink, kTruePeak,
           kDelta, kOutput, kNumParams };

    static const char* const* paramIDs();

    // Gain reduction in the most recent block (dB), for the editor's meter.
    double meterGrDb() const noexcept;

    juce::AudioProcessorValueTreeState apvts;

private:
    std::unique_ptr<airwinconsolidated::FermentLimit::FermentLimit> dsp;

    template <typename T>
    struct Scratch
    {
        std::unique_ptr<juce::AudioBuffer<T>> monoBuffer;
        void prepare(int samplesPerBlock);
        void reset();
    };
    Scratch<float> scratchF;
    Scratch<double> scratchD;

    template <typename T>
    void processBlockT(juce::AudioBuffer<T>& buffer, Scratch<T>& scratch);

    static juce::AudioProcessorValueTreeState::ParameterLayout buildLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentLimitProcessor)
};

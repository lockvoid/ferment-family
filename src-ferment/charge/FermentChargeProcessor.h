#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace airwinconsolidated::FermentCharge { class FermentCharge; }

// Standalone JUCE plugin wrapping the Ferment-owned FermentCharge DSP with a
// warm-analog UI. Single-purpose pro tool: no dropdown selector, no help tab.
class FermentChargeProcessor : public juce::AudioProcessor
{
public:
    FermentChargeProcessor();
    ~FermentChargeProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Ferment Charge"; }
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

    // Ordered to match FermentCharge::kParam*.
    enum { kInput, kCompression, kAttack, kRelease, kSaturation, kSatMode,
           kCharacter, kCharMode, kDetectorHP, kStereoMode, kSidechain,
           kScGain, kMix, kOutput, kNumParams };

    static const char* const* paramIDs();

    // Gain reduction in the most recent block (dB, >= 0), for the editor's meter.
    double meterGrDb() const noexcept;

    juce::AudioProcessorValueTreeState apvts;

private:
    std::unique_ptr<airwinconsolidated::FermentCharge::FermentCharge> dsp;

    template <typename T>
    struct Scratch
    {
        std::unique_ptr<juce::AudioBuffer<T>> monoBuffer;
        std::unique_ptr<juce::AudioBuffer<T>> silentBuffer;
        void prepare(int samplesPerBlock);
        void reset();
    };
    Scratch<float> scratchF;
    Scratch<double> scratchD;

    template <typename T>
    void processBlockT(juce::AudioBuffer<T>& buffer, Scratch<T>& scratch);

    static juce::AudioProcessorValueTreeState::ParameterLayout buildLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentChargeProcessor)
};

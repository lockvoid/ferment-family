#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../common/Biquad.h"

// Ferment EQ — 8-band parametric equalizer.  Each band has a type selector
// (Bell / LoShelf / HiShelf / LoCut / HiCut / Notch), frequency, gain, Q,
// and an enable toggle.  RBJ biquad cookbook under the hood.
class FermentEqProcessor : public juce::AudioProcessor
{
public:
    FermentEqProcessor();
    ~FermentEqProcessor() override = default;

    static constexpr int kNumBands = 8;

    enum BandType { Bell = 0, LowShelf, HighShelf, LowCut, HighCut, Notch, kNumTypes };

    static const char* typeName(int t);

    // Parameter IDs are constructed as "b{n}_{param}" with n in [1..kNumBands].
    static juce::String paramId(int band, const char* suffix);

    // Range helpers (also used by the editor for display)
    static double freqFromNorm(double v) { return 20.0 * std::pow(1000.0, v); }     // 20 Hz .. 20 kHz
    static double freqToNorm  (double f) { return std::log(f / 20.0) / std::log(1000.0); }
    static double qFromNorm   (double v) { return 0.1 * std::pow(180.0, v); }        // 0.1 .. 18
    static double gainFromNorm(double v) { return (v - 0.5) * 30.0; }                // -15 .. +15 dB
    static double outputFromNorm(double v) { return (v - 0.5) * 24.0; }              // -12 .. +12 dB

    // AudioProcessor overrides
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;

    void processBlock(juce::AudioBuffer<float>&,  juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Ferment EQ"; }
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

    // Snapshots exposed for the editor's response graph (thread-safe reads).
    struct BandSnapshot { int type; double freq; double gain; double q; bool enabled; };
    BandSnapshot snapshot(int band) const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout buildLayout();

    // Per-band coefficients (shared L+R since we recompute every block).
    ferment::Biquad bands[kNumBands];

    double currentSR = 48000.0;
    std::atomic<bool> coeffsDirty { true };

    template <typename T>
    void processBlockT(juce::AudioBuffer<T>& buffer);

    void recomputeCoeffs();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentEqProcessor)
};

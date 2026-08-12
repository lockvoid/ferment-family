#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../core/AnalyzerCore.h"

/*  Ferment Percept — the plugin wrapper (the analyzer product).

    All of the thinking is in analyzer-core; this file is plumbing.  The
    processor is a bit-exact passthrough with zero latency that feeds every
    block to one AnalyzerCore, and the editor polls a readout off it.

    Deliberately a separate directory from src-ferment/core/, which is the
    pure C++ core and stays free of JUCE.
*/
class FermentPerceptProcessor : public juce::AudioProcessor,
                                 private juce::AudioProcessorParameter::Listener
{
public:
    FermentPerceptProcessor();
    ~FermentPerceptProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Ferment Percept"; }
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

    enum Mode { Source = 0, Result };

    /*  Not const, and not accessors: both take the core lock and both bring the
        core's profile up to date with the parameter first.  That has to happen
        somewhere the transport cannot stop it — a listener switching from
        Studio to Reel with playback paused must still see the targets move. */

    /** The measurement snapshot the editor draws.  Safe to call from the
        message thread; see the lock note in the .cpp. */
    ferment::analyzer::Readout readout();

    /** The profile the core is measuring against, for the panels that print a
        target next to a measurement. */
    ferment::analyzer::Profile activeProfile();

    Mode mode() const;

    static const char* const* paramIDs();
    enum { kMode, kProfile, kReset, kNumParams };

    juce::AudioProcessorValueTreeState apvts;

private:
    template <typename T>
    void processBlockT(juce::AudioBuffer<T>& buffer);

    static juce::AudioProcessorValueTreeState::ParameterLayout buildLayout();

    /*  The momentary reset is caught where it is written rather than where the
        audio thread happens to look.  The editor raises the parameter on click
        and drops it one poll later, which is 50 ms — shorter than a single
        8192-sample block, so an edge detector living in processBlock misses
        most clicks at large buffer sizes.  A listener sees every transition
        whatever the buffer size, and whoever it fires on only sets a flag.
    */
    void parameterValueChanged(int, float newValue) override;
    void parameterGestureChanged(int, bool) override {}

    std::atomic<bool> resetRequested { false };

    /** Points the core at whichever profile the parameter selects.  Caller
        holds coreLock.  The core reads `profile` only in readout(), so this
        belongs on the reading side and nowhere near the audio thread. */
    void syncProfile();

    ferment::analyzer::AnalyzerCore core;

    /*  analyzer-core is a plain object with no thread affinity, and the spec's
        poll pattern has the editor calling readout() while the audio thread is
        inside process().  Those two genuinely collide — readout() walks the
        gate-block vector that process() appends to — so the wrapper serialises
        them.  The audio thread only ever TRIES the lock and skips feeding the
        analyser on the rare tick it loses, which costs a few milliseconds of
        measurement and never blocks audio.  Note: the underlying
        sharpness is in the core, and the core is frozen.
    */
    mutable juce::SpinLock coreLock;

    // Cached in the constructor: resolving a parameter by string on the audio
    // thread is the allocation the EQ processor got caught doing.
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* profileParam = nullptr;

    int lastProfileIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentPerceptProcessor)
};

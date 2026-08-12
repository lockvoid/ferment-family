#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../core/MasterCore.h"

#include <array>
#include <atomic>
#include <vector>

/*  Ferment Master — the plugin wrapper.

    MasterCore is the whole product; this maps it onto APVTS in display units,
    drives the two-phase Learn, and publishes what the editor needs to draw.
*/
class FermentMasterProcessor : public juce::AudioProcessor,
                               private juce::AudioProcessorParameter::Listener,
                               private juce::Timer
{
public:
    FermentMasterProcessor();
    ~FermentMasterProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Ferment Master"; }
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

    // ---- parameters -----------------------------------------------------
    // A flat namespace mirroring ChainSettings, in the units printed on the
    // faces of the standalone plugins, so a Master knob and a Charge knob
    // showing "5.5" mean the same thing.
    enum Param
    {
        kStageGain = 0, kStageWidth,
        kChargeCompression, kChargeAttack, kChargeRelease, kChargeSaturation,
        kChargeSatMode, kChargeCharacter, kChargeCharMode, kChargeHp, kChargeMix,
        kToneLow, kTonePresence, kTonePresenceQ, kToneHigh,
        kClipCeiling, kClipKnee, kClipTilt, kClipBias,
        kLimitCeiling, kLimitAttack, kLimitRelease, kLimitTruePeak,
        kPushDb,
        kBypassStage, kBypassCharge, kBypassTone, kBypassClip, kBypassLimit,
        kProfile, kLearn,
        kNumParams
    };

    static const char* const* paramIDs();

    /** The bypass parameter for a module, so the editor does not carry its own
        copy of the module-to-parameter mapping. */
    static Param bypassParamFor(ferment::master::Module m);

    // ---- Learn ----------------------------------------------------------
    /*  Started by a rising edge on the `learn` parameter, from wherever it
        comes: the face raises it, a host automation lane raises it, and both
        are caught by a parameter listener rather than by sampling the value once
        per block — a momentary press is allowed to be shorter than one buffer.
    */

    /** Phase 1 runs for at least this long before the wrapper finishes it. */
    static constexpr double learnListenSeconds = 10.0;

    struct LearnStatus
    {
        int phase = 0;              ///< 0 idle, 1 listening, 2 setting levels
        double progress = 0.0;      ///< 0..1 through phase 1
    };

    LearnStatus learnStatus() const;

    // ---- metering -------------------------------------------------------
    /*  Published from the audio thread through a seqlock rather than read
        straight off the core.  MasterCore's analyser taps are ordinary members
        that process() writes to, so a UI thread calling sourceReadout() while
        audio is running is a genuine data race — and the core is frozen.  A
        snapshot the audio thread publishes costs the same and cannot tear.
    */
    struct Snapshot
    {
        double sourceLufs = -100.0, sourceTruePeak = -100.0;
        double resultLufs = -100.0, resultTruePeak = -100.0;
        double chargeGrDb = 0.0, clipShaveDb = 0.0, limitGrDb = 0.0;
    };

    Snapshot snapshot() const;

    ferment::analyzer::Profile activeProfile() const;

    // ---- what the parameters actually became ----------------------------
    /*  The end of the mapping this wrapper exists to perform.  Read between
        blocks (the audio thread writes it), which is what a test does and what
        an offline caller does; nothing on the UI path needs it.
    */
    const ferment::policy::ChainSettings& appliedSettings() const { return core.settings(); }
    bool moduleBypassed(ferment::master::Module m) const { return core.bypassed(m); }

    juce::AudioProcessorValueTreeState apvts;

private:
    /** Meter cadence, and how often the message thread looks for a finished
        Learn to write back.  Both are UI rates; neither is in the audio path. */
    static constexpr int meterPublishHz = 20;
    static constexpr int writebackPollHz = 20;

    /** A loudness readout is a gated pass over every 400 ms block since reset,
        so the LUFS pair runs at a fraction of the meter rate.  LUFS-I is an
        integrated number; it does not move fast enough to notice. */
    static constexpr int loudnessPublishDivider = 4;

    static juce::AudioProcessorValueTreeState::ParameterLayout buildLayout();

    /** Reads the cached parameter atomics into a ChainSettings.  Audio thread,
        no allocation, no string lookups. */
    ferment::policy::ChainSettings settingsFromParameters() const;

    void syncParametersToCore();
    void publishSnapshot();
    void publishLoudness();          // message thread: the expensive half
    void beginLearnIfRequested();
    void advanceLearn(int numSamples);

    /** Learn's rising edge, from whichever thread moved the parameter. */
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int, bool) override {}

    /*  The write-back cannot wait for an editor: a Learn run with the window
        closed still has to land on the knobs, and until it does the parameter
        sync is held off.  A processor-owned timer is the whole of it — one
        atomic load per tick when nothing is pending.
    */
    void timerCallback() override;

    /** Writes a completed Learn into the visible parameters, gestures balanced,
        so the host records it as automation and the knobs move. */
    void commitLearnedSettings();

    /** Drops what the wrapper believes about an in-flight Learn.  The core
        cancels one whenever it is prepared or reset, so anything still latched
        out here — the phase the face draws, the held-off parameter sync — is
        stale and would never clear. */
    void resetLearnPublication();

    template <typename T>
    void processBlockT(juce::AudioBuffer<T>& buffer);

    ferment::master::MasterCore core;

    double sampleRate = 48000.0;

    // Cached once: getRawParameterValue builds a juce::String per call, which is
    // the audio-thread allocation the EQ processor was caught doing.
    std::array<std::atomic<float>*, kNumParams> params {};
    std::array<float, kNumParams> lastApplied {};
    bool everApplied = false;

    // float hosts get converted into these rather than into fresh vectors.
    std::vector<double> scratchL, scratchR;

    // ---- Learn state (audio thread owns it) -----------------------------
    std::atomic<bool> learnRequested { false };
    long long learnSamples = 0;
    /*  Between learnFinish() and the write-back reaching APVTS, the per-block
        parameter sync would push the OLD values straight back over the freshly
        learned ones.  This holds it off for those few frames. */
    std::atomic<bool> awaitingWriteback { false };
    std::atomic<int> publishedPhase { 0 };
    std::atomic<double> publishedProgress { 0.0 };

    ferment::policy::ChainSettings learned;
    std::atomic<bool> settingsReady { false };
    std::atomic<bool> pushReady { false };
    std::atomic<double> learnedPushDb { 0.0 };

    // ---- snapshot seqlock -----------------------------------------------
    /*  Single writer (audio), single reader (UI), no locks: the writer bumps the
        counter to odd before writing and to even after, and the reader retries
        while the counter moved.  The reader can spin; the writer never can. */
    Snapshot published;
    // Written by the message-thread timer, read by the audio thread when it
    // assembles a snapshot. Plain atomics: four independent doubles that are
    // only ever displayed, never used to make audio decisions.
    std::atomic<double> loudnessSourceLufs { -100.0 }, loudnessSourceTp { -100.0 };
    std::atomic<double> loudnessResultLufs { -100.0 }, loudnessResultTp { -100.0 };
    std::atomic<unsigned> snapshotSeq { 0 };
    long long snapshotCountdown = 0;
    int loudnessDivider = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentMasterProcessor)
};

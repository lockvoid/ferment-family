#include "FermentMasterProcessor.h"
#include "FermentMasterEditor.h"

#include <cmath>

namespace FA = ferment::analyzer;
namespace FM = ferment::master;

const char* const* FermentMasterProcessor::paramIDs()
{
    static const char* const ids[kNumParams] = {
        "stage_gain", "stage_width",
        "charge_compression", "charge_attack", "charge_release", "charge_saturation",
        "charge_satmode", "charge_character", "charge_charmode", "charge_hp", "charge_mix",
        "tone_low", "tone_presence", "tone_presenceq", "tone_high",
        "clip_ceiling", "clip_knee", "clip_tilt", "clip_bias",
        "limit_ceiling", "limit_attack", "limit_release", "limit_truepeak",
        "push_db",
        "bypass_stage", "bypass_charge", "bypass_tone", "bypass_clip", "bypass_limit",
        "profile", "learn"
    };
    return ids;
}

FermentMasterProcessor::Param FermentMasterProcessor::bypassParamFor(FM::Module m)
{
    switch (m)
    {
        case FM::ModStage:  return kBypassStage;
        case FM::ModCharge: return kBypassCharge;
        case FM::ModTone:   return kBypassTone;
        case FM::ModClip:   return kBypassClip;
        case FM::ModLimit:
        default:            return kBypassLimit;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout
FermentMasterProcessor::buildLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const char* const* ids = paramIDs();

    // Defaults are ferment-policy's studio base voicing, so an un-Learned
    // Master is already the shipped canonical chain rather than a blank one.
    const ferment::policy::ChainSettings base;

    auto addFloat = [&](int index, const char* name, float lo, float hi, float def,
                        float step = 0.0f)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(ids[index], 1), name,
            juce::NormalisableRange<float>(lo, hi, step), def));
    };

    auto addChoice = [&](int index, const char* name, juce::StringArray choices, int def)
    {
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(ids[index], 1), name, std::move(choices), def));
    };

    auto addBool = [&](int index, const char* name, bool def)
    {
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(ids[index], 1), name, def));
    };

    addFloat(kStageGain,  "Stage Gain",  -24.0f, 24.0f, (float)base.stagingDb);
    addFloat(kStageWidth, "Stage Width",   0.0f,  1.0f, (float)base.widthFactor);

    addFloat (kChargeCompression, "Charge Compression", 1.0f, 10.0f,  (float)base.charge.compression);
    addFloat (kChargeAttack,      "Charge Attack",      1.0f, 10.0f,  (float)base.charge.attack);
    addFloat (kChargeRelease,     "Charge Release",     1.0f, 10.0f,  (float)base.charge.release);
    addFloat (kChargeSaturation,  "Charge Saturation",  1.0f, 10.0f,  (float)base.charge.saturation);
    addChoice(kChargeSatMode,     "Charge Sat Mode",  { "Mild", "Moderate", "Hot" }, base.charge.satMode);
    addFloat (kChargeCharacter,   "Charge Character",   1.0f, 10.0f,  (float)base.charge.character);
    addChoice(kChargeCharMode,    "Charge Char Mode", { "Fat", "Warm", "Bright" },   base.charge.charMode);
    addChoice(kChargeHp,          "Charge Detector HP", { "Off", "100 Hz", "300 Hz" }, base.charge.detectorHp);
    addFloat (kChargeMix,         "Charge Mix",         0.0f, 100.0f, (float)base.charge.mixPct);

    addFloat(kToneLow,       "Tone Low Shelf", -12.0f, 0.0f, (float)base.eq.lowShelfDb);
    addFloat(kTonePresence,  "Tone Presence",   -6.0f, 6.0f, (float)base.eq.presenceDb);
    addFloat(kTonePresenceQ, "Tone Presence Q",  0.3f, 3.0f, (float)base.eq.presenceQ);
    addFloat(kToneHigh,      "Tone High Shelf",  0.0f, 6.0f, (float)base.eq.highShelfDb);

    addFloat(kClipCeiling, "Clip Ceiling", -24.0f,   0.0f, (float)base.clip.ceilingDb);
    addFloat(kClipKnee,    "Clip Knee",      0.0f, 100.0f, (float)base.clip.kneePct);
    addFloat(kClipTilt,    "Clip Tilt",      0.0f, 100.0f, (float)base.clip.tiltPct);
    addFloat(kClipBias,    "Clip Bias",   -100.0f, 100.0f, (float)base.clip.biasPct);

    addFloat(kLimitCeiling,  "Limit Ceiling", -24.0f,   0.0f, (float)base.limit.ceilingDbtp);
    addFloat(kLimitAttack,   "Limit Attack",   10.0f, 300.0f, (float)base.limit.attackMs);
    addFloat(kLimitRelease,  "Limit Release",   0.25f,  4.0f, (float)base.limit.releaseScale);
    addBool (kLimitTruePeak, "Limit True Peak", base.limit.truePeak);

    // Written by Learn and drivable like everything else: the sync pushes it
    // into the core, which is what brings a learned push back on session load.
    addFloat(kPushDb, "Push", -24.0f, 24.0f, 0.0f);

    addBool(kBypassStage,  "Bypass Stage",  false);
    addBool(kBypassCharge, "Bypass Charge", false);
    addBool(kBypassTone,   "Bypass Tone",   false);
    addBool(kBypassClip,   "Bypass Clip",   false);
    addBool(kBypassLimit,  "Bypass Limit",  false);

    addChoice(kProfile, "Profile", { "Studio", "Reel" }, 0);
    addBool  (kLearn,   "Learn",   false);

    return layout;
}

FermentMasterProcessor::FermentMasterProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input",   juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "FermentMaster", buildLayout())
{
    const char* const* ids = paramIDs();

    /*  Every id in the table is added by buildLayout(), so a null here is a
        typo in this file and nothing else -- and a silent 0.0 in its place would
        be an indeterminate chain, which is exactly what ChainSettings' defaults
        exist to prevent.  Resolved once, asserted once, read without a branch.
    */
    for (int i = 0; i < kNumParams; ++i)
    {
        params[(size_t)i] = apvts.getRawParameterValue(ids[i]);
        jassert(params[(size_t)i] != nullptr);
    }

    apvts.getParameter(ids[kLearn])->addListener(this);

    startTimer(1000 / writebackPollHz);
}

FermentMasterProcessor::~FermentMasterProcessor()
{
    stopTimer();
    apvts.getParameter(paramIDs()[kLearn])->removeListener(this);
}

// =============================================================================
//  Lifecycle
// =============================================================================

void FermentMasterProcessor::prepareToPlay(double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;

    core.prepare(newSampleRate, juce::jmax(1, samplesPerBlock));
    core.setProfile(activeProfile());

    scratchL.assign((size_t)juce::jmax(1, samplesPerBlock), 0.0);
    scratchR.assign((size_t)juce::jmax(1, samplesPerBlock), 0.0);

    everApplied = false;
    snapshotCountdown = 0;
    loudnessDivider = 0;
    resetLearnPublication();

    // Engines stay in line when a module is bypassed, so latency is constant —
    // a plugin whose latency moved when you clicked a bypass would make the DAW
    // re-plan the whole graph mid-playback.
    setLatencySamples(core.latencySamples());
}

void FermentMasterProcessor::reset()
{
    core.reset();
    everApplied = false;
    resetLearnPublication();
}

void FermentMasterProcessor::releaseResources()
{
    scratchL.clear();
    scratchR.clear();
}

bool FermentMasterProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

FA::Profile FermentMasterProcessor::activeProfile() const
{
    return params[(size_t)kProfile]->load() > 0.5f ? FA::profileReel() : FA::profileStudio();
}

// =============================================================================
//  Parameters -> core
// =============================================================================

ferment::policy::ChainSettings FermentMasterProcessor::settingsFromParameters() const
{
    auto value = [this](Param p) { return (double)params[(size_t)p]->load(); };

    ferment::policy::ChainSettings s;

    s.stagingDb   = value(kStageGain);
    s.widthFactor = value(kStageWidth);

    s.charge.compression = value(kChargeCompression);
    s.charge.attack      = value(kChargeAttack);
    s.charge.release     = value(kChargeRelease);
    s.charge.saturation  = value(kChargeSaturation);
    s.charge.satMode     = (int)std::lround(value(kChargeSatMode));
    s.charge.character   = value(kChargeCharacter);
    s.charge.charMode    = (int)std::lround(value(kChargeCharMode));
    s.charge.detectorHp  = (int)std::lround(value(kChargeHp));
    s.charge.mixPct      = value(kChargeMix);

    s.eq.lowShelfDb  = value(kToneLow);
    s.eq.presenceDb  = value(kTonePresence);
    s.eq.presenceQ   = value(kTonePresenceQ);
    s.eq.highShelfDb = value(kToneHigh);

    s.clip.ceilingDb = value(kClipCeiling);
    s.clip.kneePct   = value(kClipKnee);
    s.clip.tiltPct   = value(kClipTilt);
    s.clip.biasPct   = value(kClipBias);

    s.limit.ceilingDbtp  = value(kLimitCeiling);
    s.limit.attackMs     = value(kLimitAttack);
    s.limit.releaseScale = value(kLimitRelease);
    s.limit.truePeak     = value(kLimitTruePeak) > 0.5;

    // Not a knob: the loudness target belongs to the profile, and phase 2 of
    // Learn reads it straight off this field.
    s.targetLufs = activeProfile().targetLufs;

    return s;
}

void FermentMasterProcessor::syncParametersToCore()
{
    /*  Everything below is a comparison against the values last pushed into the
        core, so a steady session does no work at all.  It matters more than the
        cycles: setBypass() recomputes a crossfade step from where the fade has
        got to, so calling it every block with an unchanged value would restart
        the fade forever and the gain would never arrive.
    */
    const bool first = ! everApplied;
    bool settingsChanged = first;

    for (int i = 0; i < kNumParams; ++i)
    {
        const float v = params[(size_t)i]->load();
        const bool moved = first || v != lastApplied[(size_t)i];
        lastApplied[(size_t)i] = v;

        if (! moved)
            continue;

        if (i <= kLimitTruePeak)
        {
            settingsChanged = true;      // the ChainSettings block
        }
        else if (i == kProfile)
        {
            core.setProfile(activeProfile());
            settingsChanged = true;      // the profile carries the loudness target
        }
        else if (i >= kBypassStage && i <= kBypassLimit)
        {
            for (int m = 0; m < FM::kNumModules; ++m)
                if (i == bypassParamFor((FM::Module)m))
                    core.setBypass((FM::Module)m, v > 0.5f);
        }
        else if (i == kPushDb)
        {
            /*  This is how a learned push survives a session reload: the host
                restores the parameter, the first sync lands it back on the
                core.  Without it the knob showed the push and the audio did
                not have it — a reopened session came back quieter by exactly
                the learned amount.  It also makes Push a real knob rather
                than a readout.  When Learn's phase 2 completes it writes the
                core first and the parameter after, so this apply is a no-op
                on that path.  */
            core.setPushDb((double)v);
        }
        // kLearn drives nothing from here: it arrives through the parameter
        // listener, which catches a press shorter than one buffer.
    }

    if (settingsChanged)
        core.applySettings(settingsFromParameters());

    everApplied = true;
}

// =============================================================================
//  Learn
// =============================================================================

void FermentMasterProcessor::parameterValueChanged(int, float newValue)
{
    /*  The only parameter this listens to is `learn`, and the call can arrive on
        the audio thread (host automation) or the message thread (the face), so
        it does one store and nothing else.  Latching the edge here rather than
        sampling the parameter once per block is what makes a press survive a
        host whose buffer is longer than the press.
    */
    if (newValue > 0.5f)
        learnRequested.store(true);
}

void FermentMasterProcessor::resetLearnPublication()
{
    /*  A write-back that has already been earned is deliberately left pending:
        those values are real and still belong on the face. */
    learnSamples = 0;
    learnRequested.store(false);
    awaitingWriteback.store(false);
    publishedPhase.store(0);
    publishedProgress.store(0.0);
}

void FermentMasterProcessor::beginLearnIfRequested()
{
    // A press while a Learn is already running is answered by the one already
    // running, not queued behind it.
    if (! learnRequested.exchange(false) || core.phase() != 0)
        return;

    core.learnStart();
    learnSamples = 0;
    publishedProgress.store(0.0);
    publishedPhase.store(1);
}

void FermentMasterProcessor::advanceLearn(int numSamples)
{
    /*  Counted after the block has been processed, not before: phase one has to
        have actually heard ten seconds before it is finished, and counting up
        front would finish it one block early on every single run. */
    if (core.phase() == 1)
    {
        learnSamples += numSamples;

        const double needed = learnListenSeconds * sampleRate;
        publishedProgress.store(juce::jlimit(0.0, 1.0, (double)learnSamples / juce::jmax(1.0, needed)));

        if ((double)learnSamples >= needed)
        {
            learned = core.learnFinish();
            // Hold the parameter sync off until the write-back has landed, or
            // the next block would push the old values back over the freshly
            // learned ones.
            awaitingWriteback.store(true);
            settingsReady.store(true);
        }
    }

    const int phase = core.phase();

    if (publishedPhase.exchange(phase) == 2 && phase == 0)
    {
        // Phase two auto-completes inside the core; the push it measured is
        // only readable from out here.
        learnedPushDb.store(core.appliedPushDb());
        pushReady.store(true);
    }
}

FermentMasterProcessor::LearnStatus FermentMasterProcessor::learnStatus() const
{
    return { publishedPhase.load(), publishedProgress.load() };
}

void FermentMasterProcessor::timerCallback()
{
    commitLearnedSettings();
    publishLoudness();
}

/*  AnalyzerCore::readout() is not a cheap accessor: it walks the gating
    history, whose length grows with the session, so its cost climbs from
    ~20 us to milliseconds over hours. Calling it twice from processBlock
    put that on the audio thread, where at 128 samples it would eventually
    exceed the block deadline. The engine meters below it are O(1) member
    reads and stay where they are.
*/
void FermentMasterProcessor::publishLoudness()
{
    const auto source = core.sourceReadout();
    const auto result = core.resultReadout();

    loudnessSourceLufs.store(source.loudnessReady ? source.lufsIntegrated
                                                  : source.lufsShortTerm);
    loudnessSourceTp.store(source.truePeakDb);
    loudnessResultLufs.store(result.loudnessReady ? result.lufsIntegrated
                                                  : result.lufsShortTerm);
    loudnessResultTp.store(result.truePeakDb);
}

void FermentMasterProcessor::commitLearnedSettings()
{
    const bool haveSettings = settingsReady.exchange(false);
    const bool havePush = pushReady.exchange(false);

    if (! haveSettings && ! havePush)
        return;

    auto set = [this](Param index, double value)
    {
        auto* param = apvts.getParameter(paramIDs()[index]);

        // Balanced gestures: the host records this as a real edit, so Learn is
        // undoable and shows up in an automation lane like any knob move.
        param->beginChangeGesture();
        param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, param->convertTo0to1((float)value)));
        param->endChangeGesture();
    };

    if (haveSettings)
    {
        const auto s = learned;

        set(kStageGain,  s.stagingDb);
        set(kStageWidth, s.widthFactor);

        set(kChargeCompression, s.charge.compression);
        set(kChargeAttack,      s.charge.attack);
        set(kChargeRelease,     s.charge.release);
        set(kChargeSaturation,  s.charge.saturation);
        set(kChargeSatMode,     s.charge.satMode);
        set(kChargeCharacter,   s.charge.character);
        set(kChargeCharMode,    s.charge.charMode);
        set(kChargeHp,          s.charge.detectorHp);
        set(kChargeMix,         s.charge.mixPct);

        set(kToneLow,       s.eq.lowShelfDb);
        set(kTonePresence,  s.eq.presenceDb);
        set(kTonePresenceQ, s.eq.presenceQ);
        set(kToneHigh,      s.eq.highShelfDb);

        set(kClipCeiling, s.clip.ceilingDb);
        set(kClipKnee,    s.clip.kneePct);
        set(kClipTilt,    s.clip.tiltPct);
        set(kClipBias,    s.clip.biasPct);

        set(kLimitCeiling,  s.limit.ceilingDbtp);
        set(kLimitAttack,   s.limit.attackMs);
        set(kLimitRelease,  s.limit.releaseScale);
        set(kLimitTruePeak, s.limit.truePeak ? 1.0 : 0.0);

        awaitingWriteback.store(false);
    }

    if (havePush)
        set(kPushDb, learnedPushDb.load());
}

// =============================================================================
//  Metering
// =============================================================================

void FermentMasterProcessor::publishSnapshot()
{
    Snapshot next;

    next.chargeGrDb  = core.chargeGrDb();
    next.clipShaveDb = core.clipShaveDb();
    next.limitGrDb   = core.limitGrDb();

    // Loudness comes from the message-thread timer (publishLoudness), which
    // is the only place readout() is now called.
    next.sourceLufs     = loudnessSourceLufs.load();
    next.sourceTruePeak = loudnessSourceTp.load();
    next.resultLufs     = loudnessResultLufs.load();
    next.resultTruePeak = loudnessResultTp.load();

    // Odd counter = mid-write; the reader retries rather than reading half a
    // frame.  The writer never waits for anything.
    snapshotSeq.fetch_add(1, std::memory_order_release);
    published = next;
    snapshotSeq.fetch_add(1, std::memory_order_release);
}

FermentMasterProcessor::Snapshot FermentMasterProcessor::snapshot() const
{
    Snapshot copy;

    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const unsigned before = snapshotSeq.load(std::memory_order_acquire);

        if ((before & 1u) == 0u)
        {
            copy = published;

            if (snapshotSeq.load(std::memory_order_acquire) == before)
                return copy;
        }
    }

    // Eight collisions in a row means the audio thread is publishing far faster
    // than expected; showing the last consistent frame beats spinning the UI.
    return copy;
}

// =============================================================================
//  Audio
// =============================================================================

template <typename T>
void FermentMasterProcessor::processBlockT(juce::AudioBuffer<T>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();

    // The scratch is the only proof this has been prepared, and an unprepared
    // MasterCore::process() spins forever on a zero-length chunk.  Nothing below
    // touches the core until that is settled.
    const int capacity = (int)scratchL.size();

    if (n <= 0 || buffer.getNumChannels() < 2 || capacity <= 0)
        return;

    if (! awaitingWriteback.load())
        syncParametersToCore();

    beginLearnIfRequested();

    auto* left  = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);

    /*  A host is allowed to hand over a block longer than the one it promised in
        prepareToPlay.  Chunking through the scratch covers that without ever
        allocating on the audio thread — and the core chunks internally too, so
        the result is identical either way.
    */
    for (int pos = 0; pos < n; pos += capacity)
    {
        const int chunk = juce::jmin(capacity, n - pos);

        for (int i = 0; i < chunk; ++i)
        {
            /*  Sanitised at the boundary, like AnalyzerCore does for itself:
                one non-finite sample from an upstream plugin would otherwise
                lodge in the recursive state of every engine downstream —
                filters, envelopes, the limiter's box averagers — and the chain
                would output NaN for the rest of the session.  A dropped sample
                is a click that ends; a poisoned limiter does not.  */
            const double l = (double)left[pos + i];
            const double r = (double)right[pos + i];
            scratchL[(size_t)i] = std::isfinite(l) ? l : 0.0;
            scratchR[(size_t)i] = std::isfinite(r) ? r : 0.0;
        }

        core.process(scratchL.data(), scratchR.data(), chunk);

        for (int i = 0; i < chunk; ++i)
        {
            left[pos + i]  = (T)scratchL[(size_t)i];
            right[pos + i] = (T)scratchR[(size_t)i];
        }
    }

    advanceLearn(n);

    snapshotCountdown -= n;

    if (snapshotCountdown <= 0)
    {
        snapshotCountdown = (long long)(sampleRate / meterPublishHz);
        publishSnapshot();
    }
}

void FermentMasterProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processBlockT(buffer);
}

void FermentMasterProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processBlockT(buffer);
}

juce::AudioProcessorEditor* FermentMasterProcessor::createEditor()
{
    return new FermentMasterEditor(*this);
}

void FermentMasterProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void FermentMasterProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));

    if (xml == nullptr || ! xml->hasTagName(apvts.state.getType()))
        return;

    apvts.replaceState(juce::ValueTree::fromXml(*xml));

    /*  `learn` is momentary, and a session saved inside the 50 ms the button
        holds it up saves it raised.  Restoring that would look like a press and
        re-master the track fourteen seconds later; loading a session must never
        start a Learn.
    */
    learnRequested.store(false);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FermentMasterProcessor();
}

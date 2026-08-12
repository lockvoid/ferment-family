#include "FermentPerceptProcessor.h"
#include "FermentPerceptEditor.h"

namespace FA = ferment::analyzer;

const char* const* FermentPerceptProcessor::paramIDs()
{
    static const char* const ids[kNumParams] = { "mode", "profile", "reset" };
    return ids;
}

juce::AudioProcessorValueTreeState::ParameterLayout
FermentPerceptProcessor::buildLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const char* const* ids = paramIDs();

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ids[kMode], 1), "Mode",
        juce::StringArray { "Source", "Result" }, Source));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ids[kProfile], 1), "Profile",
        juce::StringArray { "Studio", "Reel" }, 0));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ids[kReset], 1), "Reset", false));

    return layout;
}

FermentPerceptProcessor::FermentPerceptProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input",   juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "FermentAnalyzer", buildLayout())
{
    const char* const* ids = paramIDs();
    modeParam    = apvts.getRawParameterValue(ids[kMode]);
    profileParam = apvts.getRawParameterValue(ids[kProfile]);

    if (auto* reset = apvts.getParameter(ids[kReset]))
        reset->addListener(this);
}

FermentPerceptProcessor::~FermentPerceptProcessor()
{
    if (auto* reset = apvts.getParameter(paramIDs()[kReset]))
        reset->removeListener(this);
}

void FermentPerceptProcessor::parameterValueChanged(int, float newValue)
{
    // Rising edge only, so an automation lane parked at 1 does not wipe the
    // measurement for as long as it stays there.
    if (newValue > 0.5f)
        resetRequested.store(true);
}

void FermentPerceptProcessor::prepareToPlay(double sampleRate, int)
{
    const juce::SpinLock::ScopedLockType lock(coreLock);
    core.prepare(sampleRate);
    lastProfileIndex = -1;      // force the profile back onto the fresh core

    // A passthrough that reported latency would push the whole session around
    // for a plugin that does not touch a sample.
    setLatencySamples(0);
}

void FermentPerceptProcessor::reset()
{
    const juce::SpinLock::ScopedLockType lock(coreLock);
    core.reset();
}

void FermentPerceptProcessor::releaseResources() {}

bool FermentPerceptProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const bool inValid  = layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
                       || layouts.getMainInputChannelSet()  == juce::AudioChannelSet::mono();
    const bool outValid = layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
                       || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
    const bool sizeOk   = layouts.getMainInputChannelSet().size()
                       <= layouts.getMainOutputChannelSet().size();
    return inValid && outValid && sizeOk;
}

template <typename T>
void FermentPerceptProcessor::processBlockT(juce::AudioBuffer<T>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();

    /*  Guard the buffer, not the bus.  A channel count taken from the bus says
        nothing about the buffer that actually arrived, and getReadPointer(0) on
        a channel-less buffer returns a null pointer that goes straight into the
        core — which is what a host handing over an empty buffer used to do.
    */
    if (n <= 0 || buffer.getNumChannels() <= 0)
        return;

    // isBusesLayoutSupported only admits mono or stereo in, so "not stereo"
    // means one real input channel that has to feed both outputs.
    auto* inBus = getBus(true, 0);
    const bool stereo = buffer.getNumChannels() >= 2
                     && inBus != nullptr && inBus->getNumberOfChannels() >= 2;

    /*  Nothing below writes to the buffer.  A passthrough that "copies input to
        output" by hand would be a copy that can go wrong; leaving the samples
        untouched is bit-exact by construction, and the null test proves it.
        The one exception is a mono input feeding a stereo output, where the
        second channel has no source of its own.
    */
    const T* left  = buffer.getReadPointer(0);
    const T* right = stereo ? buffer.getReadPointer(1) : left;

    const juce::SpinLock::ScopedTryLockType lock(coreLock);

    if (lock.isLocked())
    {
        // Cleared only once the reset has actually happened, so losing the
        // try-lock delays the request by a block rather than swallowing it.
        if (resetRequested.exchange(false))
            core.reset();

        core.process(left, right, n);
    }

    if (! stereo && buffer.getNumChannels() >= 2)
        buffer.copyFrom(1, 0, buffer, 0, 0, n);
}

void FermentPerceptProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processBlockT(buffer);
}

void FermentPerceptProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processBlockT(buffer);
}

void FermentPerceptProcessor::syncProfile()
{
    const int index = profileParam != nullptr ? (int) profileParam->load() : 0;

    if (index != lastProfileIndex)
    {
        core.setProfile(index == 1 ? FA::profileReel() : FA::profileStudio());
        lastProfileIndex = index;
    }
}

FA::Readout FermentPerceptProcessor::readout()
{
    const juce::SpinLock::ScopedLockType lock(coreLock);
    syncProfile();
    return core.readout();
}

FA::Profile FermentPerceptProcessor::activeProfile()
{
    const juce::SpinLock::ScopedLockType lock(coreLock);
    syncProfile();
    return core.activeProfile();
}

FermentPerceptProcessor::Mode FermentPerceptProcessor::mode() const
{
    return modeParam != nullptr && modeParam->load() > 0.5f ? Result : Source;
}

juce::AudioProcessorEditor* FermentPerceptProcessor::createEditor()
{
    return new FermentPerceptEditor(*this);
}

void FermentPerceptProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void FermentPerceptProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FermentPerceptProcessor();
}

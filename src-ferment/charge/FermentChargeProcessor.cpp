#include "FermentChargeProcessor.h"
#include "FermentChargeEditor.h"

#include "FermentCharge.h"

namespace FC = airwinconsolidated::FermentCharge;

const char* const* FermentChargeProcessor::paramIDs()
{
    static const char* const ids[kNumParams] = {
        "input", "compression", "attack", "release", "saturation", "satmode",
        "character", "charmode", "detectorhp", "stereomode", "sidechain",
        "scgain", "mix", "output"
    };
    return ids;
}

juce::AudioProcessorValueTreeState::ParameterLayout
FermentChargeProcessor::buildLayout()
{
    using Param = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // All params are 0..1, matching the underlying DSP's normalisation.
    // Display formatting lives in the editor; the host only sees 0..1.
    const char* const* ids = paramIDs();
    auto add = [&](int index, const char* name, float def) {
        layout.add(std::make_unique<Param>(juce::ParameterID(ids[index], 1), name,
                                           juce::NormalisableRange<float>(0.f, 1.f), def));
    };

    add(kInput,       "Input",       0.5f);
    add(kCompression, "Compression", 0.0f);
    add(kAttack,      "Attack",      0.3667f);   // reference default 4.30
    add(kRelease,     "Release",     0.4444f);   // reference default 5.00
    add(kSaturation,  "Saturation",  0.0f);
    add(kSatMode,     "Sat Mode",    0.0f);      // Mild
    add(kCharacter,   "Character",   0.0f);
    add(kCharMode,    "Char Mode",   0.0f);      // Fat
    add(kDetectorHP,  "Detector HP", 0.0f);      // Off
    add(kStereoMode,  "Stereo Mode", 0.0f);      // Stereo Link
    add(kSidechain,   "Sidechain",   0.0f);
    add(kScGain,      "SC Gain",     0.5f);
    add(kMix,         "Mix",         1.0f);      // full wet
    add(kOutput,      "Output",      0.5f);
    return layout;
}

FermentChargeProcessor::FermentChargeProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input",     juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
                         .withOutput("Output",   juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "FermentCharge", buildLayout())
{
    dsp = std::make_unique<FC::FermentCharge>(0);
}

FermentChargeProcessor::~FermentChargeProcessor() = default;

template <typename T>
void FermentChargeProcessor::Scratch<T>::prepare(int samplesPerBlock)
{
    monoBuffer.reset(new juce::AudioBuffer<T>(1, samplesPerBlock));
    silentBuffer.reset(new juce::AudioBuffer<T>(2, samplesPerBlock));
    silentBuffer->clear();
}

template <typename T>
void FermentChargeProcessor::Scratch<T>::reset()
{
    monoBuffer.reset();
    silentBuffer.reset();
}

void FermentChargeProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Prepare both precisions — some hosts switch after prepareToPlay.
    scratchF.prepare(samplesPerBlock);
    scratchD.prepare(samplesPerBlock);
    dsp->setSampleRate((float)sampleRate);
    // Envelopes and filter memory belong to the old rate and the old transport
    // position; carrying them over is audible as a burst of wrong gain.
    dsp->reset();
}

void FermentChargeProcessor::reset()
{
    // Hosts call this on transport stop (VST3 setProcessing(false), AU Reset).
    dsp->reset();
}

void FermentChargeProcessor::releaseResources()
{
    scratchF.reset();
    scratchD.reset();
}

bool FermentChargeProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const bool inValid  = layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
                       || layouts.getMainInputChannelSet()  == juce::AudioChannelSet::mono();
    const bool outValid = layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
                       || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
    const bool sizeOk   = layouts.getMainInputChannelSet().size()
                       <= layouts.getMainOutputChannelSet().size();
    const auto scSet    = layouts.getChannelSet(true, 1);
    const bool scValid  = scSet == juce::AudioChannelSet::disabled()
                       || scSet == juce::AudioChannelSet::stereo()
                       || scSet == juce::AudioChannelSet::mono();
    return inValid && outValid && sizeOk && scValid;
}

template <typename T>
void FermentChargeProcessor::processBlockT(juce::AudioBuffer<T>& buffer, Scratch<T>& scratch)
{
    juce::ScopedNoDenormals noDenormals;

    const char* const* ids = paramIDs();
    for (int i = 0; i < kNumParams; ++i)
        if (auto* v = apvts.getRawParameterValue(ids[i]))
            dsp->setParameter(i, v->load());

    auto* inBus  = getBus(true,  0);
    auto* outBus = getBus(false, 0);
    if (!inBus || !outBus || inBus->getNumberOfChannels() == 0) return;

    if (inBus->getNumberOfChannels() == 1 && scratch.monoBuffer)
        scratch.monoBuffer->copyFrom(0, 0, buffer, 0, 0, buffer.getNumSamples());

    const T* inputs[4];
    T*       outputs[2];
    inputs[0] = buffer.getReadPointer(0);
    inputs[1] = inBus->getNumberOfChannels() == 2 ? buffer.getReadPointer(1)
                                                  : scratch.monoBuffer->getReadPointer(0);
    outputs[0] = buffer.getWritePointer(0);
    outputs[1] = outBus->getNumberOfChannels() == 2 ? buffer.getWritePointer(1)
                                                    : scratch.monoBuffer->getWritePointer(0);

    auto scBuffer = getBusBuffer(buffer, true, 1);
    const int scCh = scBuffer.getNumChannels();
    if (scCh > 0)
    {
        inputs[2] = scBuffer.getReadPointer(0);
        inputs[3] = scCh >= 2 ? scBuffer.getReadPointer(1) : scBuffer.getReadPointer(0);
    }
    else
    {
        const int mainIn = inBus->getNumberOfChannels();
        const int totalCh = buffer.getNumChannels();
        if (totalCh > mainIn)
        {
            inputs[2] = buffer.getReadPointer(mainIn);
            inputs[3] = totalCh > mainIn + 1 ? buffer.getReadPointer(mainIn + 1)
                                             : buffer.getReadPointer(mainIn);
        }
        else
        {
            inputs[2] = scratch.silentBuffer->getReadPointer(0);
            inputs[3] = scratch.silentBuffer->getReadPointer(1);
        }
    }

    if (!inputs[0] || !inputs[1] || !inputs[2] || !inputs[3] || !outputs[0] || !outputs[1])
        return;

    if constexpr (std::is_same_v<T, float>)
        dsp->processReplacing((float**)inputs, (float**)outputs, buffer.getNumSamples());
    else
        dsp->processDoubleReplacing((double**)inputs, (double**)outputs, buffer.getNumSamples());
}

void FermentChargeProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processBlockT(buffer, scratchF);
}

void FermentChargeProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processBlockT(buffer, scratchD);
}

juce::AudioProcessorEditor* FermentChargeProcessor::createEditor()
{
    return new FermentChargeEditor(*this);
}

void FermentChargeProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void FermentChargeProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FermentChargeProcessor();
}

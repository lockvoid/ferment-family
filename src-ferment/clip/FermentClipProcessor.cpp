#include "FermentClipProcessor.h"
#include "FermentClipEditor.h"

#include "FermentClip.h"

namespace FC = airwinconsolidated::FermentClip;

const char* const* FermentClipProcessor::paramIDs()
{
    static const char* const ids[kNumParams] = {
        "input", "ceiling", "knee", "tilt", "bias", "mix", "output",
        "autogain", "delta"
    };
    return ids;
}

juce::AudioProcessorValueTreeState::ParameterLayout
FermentClipProcessor::buildLayout()
{
    using Param = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // All params are 0..1, matching the underlying DSP's normalisation.
    const char* const* ids = paramIDs();
    auto add = [&](int index, const char* name, float def) {
        layout.add(std::make_unique<Param>(juce::ParameterID(ids[index], 1), name,
                                           juce::NormalisableRange<float>(0.f, 1.f), def));
    };

    add(kInput,    "Input",     0.5f);
    add(kCeiling,  "Ceiling",   1.0f);    //  0 dBFS
    add(kKnee,     "Knee",      0.35f);   //  prototype voicing
    add(kTilt,     "Tilt",      0.5f);    //  prototype voicing
    add(kBias,     "Bias",      0.5f);    //  symmetric
    add(kMix,      "Mix",       1.0f);
    add(kOutput,   "Output",    0.5f);
    add(kAutoGain, "Auto Gain", 0.0f);
    add(kDelta,    "Delta",     0.0f);
    return layout;
}

FermentClipProcessor::FermentClipProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input",   juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "FermentClip", buildLayout())
{
    dsp = std::make_unique<FC::FermentClip>(0);
}

FermentClipProcessor::~FermentClipProcessor() = default;

double FermentClipProcessor::meterShaveDb() const noexcept
{
    return dsp ? dsp->meterShaveDb() : 0.0;
}

template <typename T>
void FermentClipProcessor::Scratch<T>::prepare(int samplesPerBlock)
{
    monoBuffer.reset(new juce::AudioBuffer<T>(1, samplesPerBlock));
}

template <typename T>
void FermentClipProcessor::Scratch<T>::reset()
{
    monoBuffer.reset();
}

void FermentClipProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    scratchF.prepare(samplesPerBlock);
    scratchD.prepare(samplesPerBlock);
    dsp->setSampleRate((float)sampleRate);
    dsp->reset();
    setLatencySamples(FC::kLatencySamples);
}

void FermentClipProcessor::reset()
{
    dsp->reset();
}

void FermentClipProcessor::releaseResources()
{
    scratchF.reset();
    scratchD.reset();
}

bool FermentClipProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
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
void FermentClipProcessor::processBlockT(juce::AudioBuffer<T>& buffer, Scratch<T>& scratch)
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

    const T* inputs[2];
    T*       outputs[2];
    inputs[0] = buffer.getReadPointer(0);
    inputs[1] = inBus->getNumberOfChannels() == 2 ? buffer.getReadPointer(1)
                                                  : scratch.monoBuffer->getReadPointer(0);
    outputs[0] = buffer.getWritePointer(0);
    outputs[1] = outBus->getNumberOfChannels() == 2 ? buffer.getWritePointer(1)
                                                    : scratch.monoBuffer->getWritePointer(0);

    if (!inputs[0] || !inputs[1] || !outputs[0] || !outputs[1]) return;

    if constexpr (std::is_same_v<T, float>)
        dsp->processReplacing((float**)inputs, (float**)outputs, buffer.getNumSamples());
    else
        dsp->processDoubleReplacing((double**)inputs, (double**)outputs, buffer.getNumSamples());
}

void FermentClipProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processBlockT(buffer, scratchF);
}

void FermentClipProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processBlockT(buffer, scratchD);
}

juce::AudioProcessorEditor* FermentClipProcessor::createEditor()
{
    return new FermentClipEditor(*this);
}

void FermentClipProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void FermentClipProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FermentClipProcessor();
}

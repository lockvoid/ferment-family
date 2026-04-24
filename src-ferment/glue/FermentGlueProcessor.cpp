#include "FermentGlueProcessor.h"
#include "FermentGlueEditor.h"

#include "../../src/autogen_airwin/GlueBlue.h"

namespace GB = airwinconsolidated::GlueBlue;

juce::AudioProcessorValueTreeState::ParameterLayout
FermentGlueProcessor::buildLayout()
{
    using Param = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // All params are 0..1 — same normalisation as the underlying GlueBlue DSP.
    // Display formatting lives in the editor; VST3 automation just sees 0..1.
    auto addFloat = [&](const char* id, const char* name, float def) {
        layout.add(std::make_unique<Param>(juce::ParameterID(id, 1), name,
                                           juce::NormalisableRange<float>(0.f, 1.f), def));
    };
    addFloat("threshold", "Threshold", 0.5f);
    addFloat("ratio",     "Ratio",     0.0f);
    addFloat("attack",    "Attack",    0.3f);
    addFloat("release",   "Release",   0.5f);
    addFloat("makeup",    "Makeup",    0.0f);
    addFloat("range",     "Range",     1.0f);
    addFloat("drywet",    "Dry/Wet",   1.0f);
    addFloat("sidechain", "SideChn",   0.0f);
    addFloat("schpf",     "SC HPF",    0.2f);
    addFloat("softclip",  "SoftClip",  0.0f);
    return layout;
}

FermentGlueProcessor::FermentGlueProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input",     juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
                         .withOutput("Output",   juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "FermentGlue", buildLayout())
{
    dsp = std::make_unique<GB::GlueBlue>(0);
}

FermentGlueProcessor::~FermentGlueProcessor() = default;

template <typename T>
void FermentGlueProcessor::Scratch<T>::prepare(int samplesPerBlock)
{
    monoBuffer.reset(new juce::AudioBuffer<T>(1, samplesPerBlock));
    silentBuffer.reset(new juce::AudioBuffer<T>(2, samplesPerBlock));
    silentBuffer->clear();
}

template <typename T>
void FermentGlueProcessor::Scratch<T>::reset()
{
    monoBuffer.reset();
    silentBuffer.reset();
}

void FermentGlueProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    if (isUsingDoublePrecision()) scratchD.prepare(samplesPerBlock);
    else                          scratchF.prepare(samplesPerBlock);
    // Prepare both in case host switches precision (CLAP quirk).
    scratchF.prepare(samplesPerBlock);
    scratchD.prepare(samplesPerBlock);
    dsp->setSampleRate((float)sampleRate);
}

void FermentGlueProcessor::releaseResources()
{
    scratchF.reset();
    scratchD.reset();
}

bool FermentGlueProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
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
void FermentGlueProcessor::processBlockT(juce::AudioBuffer<T>& buffer, Scratch<T>& scratch)
{
    juce::ScopedNoDenormals noDenormals;

    // Sync DSP params from APVTS.
    const char* ids[kNumParams] = {
        "threshold","ratio","attack","release","makeup",
        "range","drywet","sidechain","schpf","softclip"
    };
    for (int i = 0; i < kNumParams; ++i)
    {
        if (auto* p = apvts.getRawParameterValue(ids[i]))
            dsp->setParameter(i, p->load());
    }

    auto* inBus  = getBus(true,  0);
    auto* outBus = getBus(false, 0);
    if (!inBus || !outBus || inBus->getNumberOfChannels() == 0) return;

    // Mono main handling — same pattern as AWConsolidatedProcessor.
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

    // Sidechain channels — same permissive resolution as the consolidated plugin.
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

void FermentGlueProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processBlockT(buffer, scratchF);
}

void FermentGlueProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processBlockT(buffer, scratchD);
}

juce::AudioProcessorEditor* FermentGlueProcessor::createEditor()
{
    return new FermentGlueEditor(*this);
}

void FermentGlueProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void FermentGlueProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FermentGlueProcessor();
}

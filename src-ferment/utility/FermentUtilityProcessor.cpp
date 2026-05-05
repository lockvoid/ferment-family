#include "FermentUtilityProcessor.h"
#include "FermentUtilityEditor.h"

const char* FermentUtilityProcessor::channelModeName(int m)
{
    switch (m) {
        case Stereo:    return "Stereo";
        case Swap:      return "Swap";
        case LeftOnly:  return "Left";
        case RightOnly: return "Right";
        case Mono:      return "Mono";
        default:        return "?";
    }
}

const char* FermentUtilityProcessor::msSoloName(int m)
{
    switch (m) {
        case SoloOff:  return "Off";
        case MidOnly:  return "Mid";
        case SideOnly: return "Side";
        default:       return "?";
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout FermentUtilityProcessor::buildLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("gain", 1), "Gain",
        juce::NormalisableRange<float>(0.f, 1.f), 0.5f));   // 0 dB default

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("mute", 1), "Mute", false));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("phaseL", 1), "Phase L", false));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("phaseR", 1), "Phase R", false));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("chanmode", 1), "Channel Mode",
        juce::StringArray { "Stereo", "Swap", "Left",  "Right", "Mono" }, Stereo));

    // Balance L / R: each knob tilts that channel's contribution (−1 fully silenced,
    // 0 neutral, +1 effectively doubled after normalisation). Ableton's model.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("balL", 1), "Balance L",
        juce::NormalisableRange<float>(0.f, 1.f), 0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("balR", 1), "Balance R",
        juce::NormalisableRange<float>(0.f, 1.f), 0.5f));

    // Width: 0 (mono) .. 1 (stereo) .. 4 (hyper-wide) via M/S scaling
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("width", 1), "Width",
        juce::NormalisableRange<float>(0.f, 1.f), 0.25f));   // 0.25 norm → width=1.0 (100%)

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("mssolo", 1), "M/S Solo",
        juce::StringArray { "Off", "Mid", "Side" }, SoloOff));

    // DC filter toggle: removes sub-5 Hz DC offset
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("dc", 1), "DC Filter", false));

    // Bass Mono: below the set frequency, L and R are summed to mono
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("bassmono", 1), "Bass Mono", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("bassmonofreq", 1), "Bass Mono Freq",
        juce::NormalisableRange<float>(0.f, 1.f), 0.28f));  // ~120 Hz default

    return layout;
}

FermentUtilityProcessor::FermentUtilityProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input",   juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "FermentUtility", buildLayout())
{}

void FermentUtilityProcessor::prepareToPlay(double sr, int)
{
    dsp.setSampleRate(static_cast<float>(sr));
}

bool FermentUtilityProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const bool inValid  = layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
                       || layouts.getMainInputChannelSet()  == juce::AudioChannelSet::mono();
    const bool outValid = layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
                       || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
    const bool sizeOk   = layouts.getMainInputChannelSet().size()
                       <= layouts.getMainOutputChannelSet().size();
    return inValid && outValid && sizeOk;
}

void FermentUtilityProcessor::syncParamsToDSP()
{
    using namespace airwinconsolidated::FermentUtility;
    const float chanScale = 1.0f / float(kNumChannelModes - 1);
    const float msScale   = 1.0f / float(kNumMSSolo - 1);

    dsp.setParameter(kParamGain,         apvts.getRawParameterValue("gain")->load());
    dsp.setParameter(kParamMute,         apvts.getRawParameterValue("mute")->load());
    dsp.setParameter(kParamPhaseL,       apvts.getRawParameterValue("phaseL")->load());
    dsp.setParameter(kParamPhaseR,       apvts.getRawParameterValue("phaseR")->load());
    dsp.setParameter(kParamChannelMode,  apvts.getRawParameterValue("chanmode")->load() * chanScale);
    dsp.setParameter(kParamBalanceL,     apvts.getRawParameterValue("balL")->load());
    dsp.setParameter(kParamBalanceR,     apvts.getRawParameterValue("balR")->load());
    dsp.setParameter(kParamWidth,        apvts.getRawParameterValue("width")->load());
    dsp.setParameter(kParamMSSolo,       apvts.getRawParameterValue("mssolo")->load() * msScale);
    dsp.setParameter(kParamDC,           apvts.getRawParameterValue("dc")->load());
    dsp.setParameter(kParamBassMono,     apvts.getRawParameterValue("bassmono")->load());
    dsp.setParameter(kParamBassMonoFreq, apvts.getRawParameterValue("bassmonofreq")->load());
}

template <typename T>
void FermentUtilityProcessor::processBlockT(juce::AudioBuffer<T>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    syncParamsToDSP();

    auto* inBus = getBus(true, 0);
    const int numIn = inBus ? inBus->getNumberOfChannels() : 2;
    const int numSamples = buffer.getNumSamples();

    T* L = buffer.getWritePointer(0);
    T* R = (numIn >= 2) ? buffer.getWritePointer(1) : L;  // mono: alias L

    T* in[2]  = { L, R };
    T* out[2] = { L, R };
    if constexpr (std::is_same_v<T, float>) {
        dsp.processReplacing(in, out, numSamples);
    } else {
        dsp.processDoubleReplacing(in, out, numSamples);
    }
}

void FermentUtilityProcessor::processBlock(juce::AudioBuffer<float>&  buffer, juce::MidiBuffer&) { processBlockT(buffer); }
void FermentUtilityProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) { processBlockT(buffer); }

juce::AudioProcessorEditor* FermentUtilityProcessor::createEditor()
{
    return new FermentUtilityEditor(*this);
}

void FermentUtilityProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void FermentUtilityProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FermentUtilityProcessor();
}

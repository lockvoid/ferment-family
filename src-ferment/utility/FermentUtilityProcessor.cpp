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

    return layout;
}

FermentUtilityProcessor::FermentUtilityProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input",   juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "FermentUtility", buildLayout())
{}

void FermentUtilityProcessor::prepareToPlay(double, int) {}

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

template <typename T>
void FermentUtilityProcessor::processBlockT(juce::AudioBuffer<T>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    const double gainDb = gainFromNorm(apvts.getRawParameterValue("gain")->load());
    const double gainLin = std::pow(10.0, gainDb / 20.0);
    const bool   mute    = apvts.getRawParameterValue("mute")->load()   >= 0.5f;
    const bool   phaseL  = apvts.getRawParameterValue("phaseL")->load() >= 0.5f;
    const bool   phaseR  = apvts.getRawParameterValue("phaseR")->load() >= 0.5f;
    const int    mode    = (int)apvts.getRawParameterValue("chanmode")->load();

    auto* inBus = getBus(true, 0);
    const int numIn = inBus ? inBus->getNumberOfChannels() : 2;
    const int numSamples = buffer.getNumSamples();

    T* L = buffer.getWritePointer(0);
    T* R = (numIn >= 2) ? buffer.getWritePointer(1) : nullptr;

    for (int n = 0; n < numSamples; ++n)
    {
        double l = (double)L[n];
        double r = (R != nullptr) ? (double)R[n] : l;

        // --- Phase invert ---
        if (phaseL) l = -l;
        if (phaseR) r = -r;

        // --- Gain ---
        l *= gainLin;
        r *= gainLin;

        // --- Channel mode routing ---
        switch (mode)
        {
            case Swap:      std::swap(l, r); break;
            case LeftOnly:  r = l; break;
            case RightOnly: l = r; break;
            case Mono:      { const double m = (l + r) * 0.5; l = m; r = m; break; }
            case Stereo:
            default:        break;
        }

        // --- Mute (last stage) ---
        if (mute) { l = 0.0; r = 0.0; }

        L[n] = (T)l;
        if (R) R[n] = (T)r;
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

#include "FermentEqProcessor.h"
#include "FermentEqEditor.h"

const char* FermentEqProcessor::typeName(int t)
{
    switch (t)
    {
        case Bell:      return "Bell";
        case LowShelf:  return "LoShelf";
        case HighShelf: return "HiShelf";
        case LowCut:    return "LoCut";
        case HighCut:   return "HiCut";
        case Notch:     return "Notch";
        default:        return "?";
    }
}

juce::String FermentEqProcessor::paramId(int band, const char* suffix)
{
    return juce::String("b") + juce::String(band + 1) + "_" + suffix;
}

juce::AudioProcessorValueTreeState::ParameterLayout FermentEqProcessor::buildLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Default band layout mirrors Ableton EQ Eight:
    //   Band 1 LoCut,   Band 2 LoShelf, Bands 3-6 Bells, Band 7 HiShelf, Band 8 HiCut
    const int defaultType[kNumBands] = {
        LowCut, LowShelf, Bell, Bell, Bell, Bell, HighShelf, HighCut
    };
    const double defaultHz[kNumBands] = {
        40, 120, 350, 900, 2500, 6000, 10000, 16000
    };

    const juce::StringArray typeChoices {
        "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch"
    };

    for (int b = 0; b < kNumBands; ++b)
    {
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(paramId(b, "type").toStdString(), 1),
            "Band " + juce::String(b + 1) + " Type",
            typeChoices, defaultType[b]));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(paramId(b, "freq").toStdString(), 1),
            "Band " + juce::String(b + 1) + " Freq",
            juce::NormalisableRange<float>(0.f, 1.f),
            (float)freqToNorm(defaultHz[b])));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(paramId(b, "gain").toStdString(), 1),
            "Band " + juce::String(b + 1) + " Gain",
            juce::NormalisableRange<float>(0.f, 1.f), 0.5f)); // 0 dB

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(paramId(b, "q").toStdString(), 1),
            "Band " + juce::String(b + 1) + " Q",
            juce::NormalisableRange<float>(0.f, 1.f), 0.4f)); // ~0.71

        // Default enable: 1=LoCut off, 2=LoShelf off, bells 3-6 on, 7=HiShelf off, 8=HiCut off
        const bool defOn = (b >= 2 && b <= 5);
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(paramId(b, "on").toStdString(), 1),
            "Band " + juce::String(b + 1) + " On", defOn));
    }

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("output", 1), "Output",
        juce::NormalisableRange<float>(0.f, 1.f), 0.5f)); // 0 dB

    return layout;
}

FermentEqProcessor::FermentEqProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input",   juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "FermentEq", buildLayout())
{
}

void FermentEqProcessor::prepareToPlay(double sampleRate, int)
{
    dsp.setSampleRate(static_cast<float>(sampleRate));
}

bool FermentEqProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const bool inValid  = layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
                       || layouts.getMainInputChannelSet()  == juce::AudioChannelSet::mono();
    const bool outValid = layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
                       || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
    const bool sizeOk   = layouts.getMainInputChannelSet().size()
                       <= layouts.getMainOutputChannelSet().size();
    return inValid && outValid && sizeOk;
}

void FermentEqProcessor::syncParamsToDSP()
{
    using namespace airwinconsolidated::FermentEq;

    // APVTS choice indices come back as int-valued floats (0..N-1). The DSP
    // stores type as 0..1 normalised. Convert on the way in.
    const float typeScale = 1.0f / float(kNumTypes - 1);

    for (int b = 0; b < kNumBands; ++b)
    {
        const float typeIdx = apvts.getRawParameterValue(paramId(b, "type"))->load();
        const float freqN   = apvts.getRawParameterValue(paramId(b, "freq"))->load();
        const float gainN   = apvts.getRawParameterValue(paramId(b, "gain"))->load();
        const float qN      = apvts.getRawParameterValue(paramId(b, "q"))->load();
        const float onN     = apvts.getRawParameterValue(paramId(b, "on"))->load();

        const int off = b * kBandStride;
        dsp.setParameter(off + kBandTypeOffset, typeIdx * typeScale);
        dsp.setParameter(off + kBandFreqOffset, freqN);
        dsp.setParameter(off + kBandGainOffset, gainN);
        dsp.setParameter(off + kBandQOffset,    qN);
        dsp.setParameter(off + kBandOnOffset,   onN);
    }
    dsp.setParameter(kParamOutput, apvts.getRawParameterValue("output")->load());
}

template <typename T>
void FermentEqProcessor::processBlockT(juce::AudioBuffer<T>& buffer)
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

void FermentEqProcessor::processBlock(juce::AudioBuffer<float>&  buffer, juce::MidiBuffer&) { processBlockT(buffer); }
void FermentEqProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) { processBlockT(buffer); }

juce::AudioProcessorEditor* FermentEqProcessor::createEditor()
{
    return new FermentEqEditor(*this);
}

void FermentEqProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void FermentEqProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

FermentEqProcessor::BandSnapshot FermentEqProcessor::snapshot(int band) const
{
    BandSnapshot s;
    s.type    = (int)apvts.getRawParameterValue(paramId(band, "type"))->load();
    s.freq    = freqFromNorm(apvts.getRawParameterValue(paramId(band, "freq"))->load());
    s.gain    = gainFromNorm(apvts.getRawParameterValue(paramId(band, "gain"))->load());
    s.q       = qFromNorm(apvts.getRawParameterValue(paramId(band, "q"))->load());
    s.enabled = apvts.getRawParameterValue(paramId(band, "on"))->load() >= 0.5f;
    return s;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FermentEqProcessor();
}

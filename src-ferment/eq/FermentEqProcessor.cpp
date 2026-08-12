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
    // Resolve every parameter atomic once, off the audio thread: the
    // per-block sync reads these pointers only (review finding §6.2).
    static const char* const kSuffixes[5] = { "type", "freq", "gain", "q", "on" };
    rawParams.reserve((size_t)kNumBands * 5 + 1);
    for (int b = 0; b < kNumBands; ++b)
        for (const char* s : kSuffixes)
            rawParams.push_back(apvts.getRawParameterValue(paramId(b, s)));
    rawParams.push_back(apvts.getRawParameterValue("output"));
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

    // Runs on the audio thread: only the atomics cached by the constructor
    // are read here. Building the 41 paramId strings per block allocated
    // ~160 times per callback (review finding §6.2).

    // APVTS choice indices come back as int-valued floats (0..N-1). The DSP
    // stores type as 0..1 normalised. Convert on the way in.
    const float typeScale = 1.0f / float(kNumTypes - 1);

    for (int b = 0; b < kNumBands; ++b)
    {
        const auto* r = &rawParams[(size_t)b * 5];
        const int off = b * kBandStride;
        dsp.setParameter(off + kBandTypeOffset, r[0]->load() * typeScale);
        dsp.setParameter(off + kBandFreqOffset, r[1]->load());
        dsp.setParameter(off + kBandGainOffset, r[2]->load());
        dsp.setParameter(off + kBandQOffset,    r[3]->load());
        dsp.setParameter(off + kBandOnOffset,   r[4]->load());
    }
    dsp.setParameter(kParamOutput, rawParams.back()->load());
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

    // Only the channels the DSP actually wrote: with a mono input into a stereo
    // bus, R aliases L and buffer channel 1 is whatever the host left there, so
    // summing it would drag the analyser's level around for no reason.
    pushSpectrum(buffer, numIn >= 2 ? 2 : 1);
}

// Audio thread: mono-sums the post-EQ block into the editor's FIFO.  Writes
// only what fits, so a closed or slow editor can never stall processing.
template <typename T>
void FermentEqProcessor::pushSpectrum(const juce::AudioBuffer<T>& buffer, int channels) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numCh      = juce::jlimit(1, juce::jmax(1, buffer.getNumChannels()), channels);

    const auto scope = spectrumFifo.write(numSamples);

    auto fill = [&](int startIndex, int blockSize, int offset)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            double sum = 0.0;
            for (int ch = 0; ch < numCh; ++ch)
                sum += (double) buffer.getReadPointer(ch)[offset + i];

            spectrumBuffer[(size_t) (startIndex + i)] = (float) (sum / (double) numCh);
        }
    };

    fill(scope.startIndex1, scope.blockSize1, 0);
    fill(scope.startIndex2, scope.blockSize2, scope.blockSize1);
}

int FermentEqProcessor::readSpectrumSamples(float* dest, int maxSamples)
{
    const auto scope = spectrumFifo.read(juce::jmin(maxSamples, spectrumFifo.getNumReady()));

    if (scope.blockSize1 > 0)
        std::memcpy(dest, spectrumBuffer.data() + scope.startIndex1,
                    (size_t) scope.blockSize1 * sizeof(float));

    if (scope.blockSize2 > 0)
        std::memcpy(dest + scope.blockSize1, spectrumBuffer.data() + scope.startIndex2,
                    (size_t) scope.blockSize2 * sizeof(float));

    return scope.blockSize1 + scope.blockSize2;
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

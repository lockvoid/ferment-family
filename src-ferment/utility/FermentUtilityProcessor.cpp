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
    sampleRate    = sr;
    dcPrevInL = dcPrevOutL = dcPrevInR = dcPrevOutR = 0.0;
    bmLpL = bmLpR = {};
    bmLastFreq = -1.0;
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

template <typename T>
void FermentUtilityProcessor::processBlockT(juce::AudioBuffer<T>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    const double gainDb  = gainFromNorm(apvts.getRawParameterValue("gain")->load());
    const double gainLin = std::pow(10.0, gainDb / 20.0);
    const bool   mute    = apvts.getRawParameterValue("mute")->load()   >= 0.5f;
    const bool   phaseL  = apvts.getRawParameterValue("phaseL")->load() >= 0.5f;
    const bool   phaseR  = apvts.getRawParameterValue("phaseR")->load() >= 0.5f;
    const int    mode    = (int)apvts.getRawParameterValue("chanmode")->load();
    const double balL    = balanceFromNorm(apvts.getRawParameterValue("balL")->load());
    const double balR    = balanceFromNorm(apvts.getRawParameterValue("balR")->load());
    const double width   = widthFromNorm(apvts.getRawParameterValue("width")->load());
    const int    ms      = (int)apvts.getRawParameterValue("mssolo")->load();
    const bool   dcOn    = apvts.getRawParameterValue("dc")->load()       >= 0.5f;
    const bool   bmOn    = apvts.getRawParameterValue("bassmono")->load() >= 0.5f;
    const double bmFreq  = bassMonoFromNorm(apvts.getRawParameterValue("bassmonofreq")->load());

    // Balance scalers — negative balance attenuates, positive keeps the level
    // (Ableton behaviour: moving L slider left silences L, right leaves it alone).
    const double balLScale = (balL >= 0.0) ? 1.0 : (1.0 + balL);   // balL in [-1..0] → scale 0..1
    const double balRScale = (balR >= 0.0) ? 1.0 : (1.0 + balR);

    // DC filter coefficient — one-pole HP, R = 1 - 2*pi*f/fs, f=5 Hz
    const double dcR = 1.0 - (2.0 * M_PI * 5.0 / sampleRate);

    // Bass-mono LP coefficients (2nd-order Butterworth LP, RBJ cookbook with Q=0.707).
    // Recompute only when the frequency changes to avoid per-sample math.
    if (bmOn && std::fabs(bmFreq - bmLastFreq) > 0.01)
    {
        const double f = juce::jlimit(20.0, sampleRate * 0.49, bmFreq);
        const double w = 2.0 * M_PI * f / sampleRate;
        const double cosw = std::cos(w);
        const double sinw = std::sin(w);
        const double alpha = sinw / (2.0 * 0.707);
        const double a0 = 1.0 + alpha;
        bmB0 = ((1.0 - cosw) * 0.5) / a0;
        bmB1 = (1.0 - cosw) / a0;
        bmB2 = bmB0;
        bmA1 = (-2.0 * cosw) / a0;
        bmA2 = (1.0 - alpha) / a0;
        bmLastFreq = bmFreq;
    }

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

        // --- Per-channel balance (attenuation-only model) ---
        l *= balLScale;
        r *= balRScale;

        // --- Width + Mid/Side solo via M/S encode ---
        {
            double midS  = (l + r) * 0.5;
            double sideS = (l - r) * 0.5;
            sideS *= width;
            if      (ms == MidOnly)  sideS = 0.0;
            else if (ms == SideOnly) midS  = 0.0;
            l = midS + sideS;
            r = midS - sideS;
        }

        // --- Bass mono: split each channel into low + high, sum lows to mono ---
        if (bmOn)
        {
            // LP via Transposed DF2
            const double lowL = bmB0 * l + bmLpL.z1;
            bmLpL.z1 = bmB1 * l - bmA1 * lowL + bmLpL.z2;
            bmLpL.z2 = bmB2 * l - bmA2 * lowL;

            const double lowR = bmB0 * r + bmLpR.z1;
            bmLpR.z1 = bmB1 * r - bmA1 * lowR + bmLpR.z2;
            bmLpR.z2 = bmB2 * r - bmA2 * lowR;

            const double highL = l - lowL;
            const double highR = r - lowR;
            const double lowMono = (lowL + lowR) * 0.5;
            l = lowMono + highL;
            r = lowMono + highR;
        }

        // --- DC filter: one-pole HP at ~5 Hz ---
        if (dcOn)
        {
            const double outL = l - dcPrevInL + dcR * dcPrevOutL;
            dcPrevInL = l;    dcPrevOutL = outL;   l = outL;
            const double outR = r - dcPrevInR + dcR * dcPrevOutR;
            dcPrevInR = r;    dcPrevOutR = outR;   r = outR;
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

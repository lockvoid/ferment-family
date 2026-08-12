#include "FermentEqEditor.h"
#include "FermentEqProcessor.h"

#include <cmath>

namespace T = ferment::theme;

namespace
{
    juce::String fmtFreq(double n)
    {
        const double f = FermentEqProcessor::freqFromNorm(n);
        return f >= 1000.0 ? juce::String(f / 1000.0, 2) + " k"
                           : juce::String((int)std::round(f)) + " Hz";
    }
    juce::String fmtGain(double n)
    {
        return juce::String(FermentEqProcessor::gainFromNorm(n), 1) + " dB";
    }
    juce::String fmtQ(double n)
    {
        return juce::String(FermentEqProcessor::qFromNorm(n), 2);
    }
    juce::String fmtOutput(double n)
    {
        return juce::String(FermentEqProcessor::outputFromNorm(n), 1) + " dB";
    }

    /** The graph reads and writes the EQ entirely through the processor's own
        parameter scheme and range maths, so the drawn curve and the DSP can
        never disagree. */
    ferment::EqGraphEditor::Spec makeGraphSpec(FermentEqProcessor& p)
    {
        ferment::EqGraphEditor::Spec spec;

        spec.numBands     = FermentEqProcessor::kNumBands;
        spec.paramId      = [](int band, const char* suffix) { return FermentEqProcessor::paramId(band, suffix); };
        spec.freqFromNorm = [](double v) { return FermentEqProcessor::freqFromNorm(v); };
        spec.gainFromNorm = [](double v) { return FermentEqProcessor::gainFromNorm(v); };
        spec.qFromNorm    = [](double v) { return FermentEqProcessor::qFromNorm(v); };
        spec.getSampleRate = [&p] { return p.getSampleRate(); };
        spec.typeName      = [](int t) { return juce::String(FermentEqProcessor::typeName(t)); };

        spec.bell      = FermentEqProcessor::Bell;
        spec.lowShelf  = FermentEqProcessor::LowShelf;
        spec.highShelf = FermentEqProcessor::HighShelf;
        spec.lowCut    = FermentEqProcessor::LowCut;
        spec.highCut   = FermentEqProcessor::HighCut;
        spec.notch     = FermentEqProcessor::Notch;
        spec.numTypes  = FermentEqProcessor::kNumTypes;

        spec.readSpectrumSamples = [&p](float* dest, int maxSamples)
        {
            return p.readSpectrumSamples(dest, maxSamples);
        };

        return spec;
    }

    constexpr int kKnobRowHeight = 14 + ferment::FermentKnob::standardFaceSize
                                      + ferment::FermentKnob::textHeight;
}

FermentEqEditor::FermentEqEditor(FermentEqProcessor& p)
    : AudioProcessorEditor(&p),
      processor(p),
      graph(p.apvts, makeGraphSpec(p))
{
    addAndMakeVisible(chassis);
    addAndMakeVisible(header);
    addAndMakeVisible(graph);

    bandLabel.setJustificationType(juce::Justification::centredLeft);
    bandLabel.setColour(juce::Label::textColourId, T::labelDim);
    bandLabel.setFont(T::monoTracked(10.0f, true));
    addAndMakeVisible(bandLabel);

    outputKnob = std::make_unique<ferment::FermentKnob>(processor.apvts, "output",
                                                        "OUTPUT", fmtOutput);
    addAndMakeVisible(*outputKnob);

    // Open on the first band that is actually doing something — band 1 is a
    // LoCut that ships disabled, so its GAIN knob would say nothing.
    int firstEnabled = 0;
    for (int b = 0; b < FermentEqProcessor::kNumBands; ++b)
    {
        if (processor.snapshot(b).enabled)
        {
            firstEnabled = b;
            break;
        }
    }

    graph.setSelectedBand(firstEnabled);
    showBand(graph.getSelectedBand());

    // Hooked up after the opening selection so the initial band is built once,
    // not built by setSelectedBand's callback and then again below.
    graph.onBandSelected = [this](int band) { showBand(band); };

    startTimerHz(10);
    setSize(900, 520);
}

FermentEqEditor::~FermentEqEditor() = default;

void FermentEqEditor::timerCallback()
{
    refreshBandLabel();
}

void FermentEqEditor::refreshBandLabel()
{
    const int band = graph.getSelectedBand();
    const auto snapshot = processor.snapshot(band);

    // Label::setText no-ops when the string is unchanged, so this costs nothing
    // on the ticks where nothing moved.
    bandLabel.setText("BAND " + juce::String(band + 1) + "  ·  "
                          + juce::String(FermentEqProcessor::typeName(snapshot.type)).toUpperCase()
                          + (snapshot.enabled ? "" : "  (OFF)"),
                      juce::dontSendNotification);
}

void FermentEqEditor::showBand(int band)
{
    bandKnobs.clear();

    auto add = [this, band](const char* suffix, const char* caption,
                            ferment::FermentKnob::Formatter formatter)
    {
        bandKnobs.push_back(std::make_unique<ferment::FermentKnob>(
            processor.apvts, FermentEqProcessor::paramId(band, suffix),
            caption, std::move(formatter)));
        addAndMakeVisible(*bandKnobs.back());
    };

    add("freq", "FREQ", fmtFreq);
    add("gain", "GAIN", fmtGain);
    add("q",    "Q",    fmtQ);

    refreshBandLabel();

    resized();
}

void FermentEqEditor::paint(juce::Graphics& g)
{
    g.fillAll(T::bg);
}

void FermentEqEditor::resized()
{
    auto inner = ferment::ChassisPanel::layoutFrame(getLocalBounds(), chassis, header);

    auto row = inner.removeFromBottom(kKnobRowHeight);
    inner.removeFromBottom(8);
    graph.setBounds(inner);

    bandLabel.setBounds(row.removeFromTop(14));

    // Output sits apart on the right, at the same face size as the band trio,
    // so the row does not mix two knob sizes the way it used to.
    using Knob = ferment::FermentKnob;

    const int face  = Knob::faceSizeFor(row, 4, 1);
    const int cellW = face + Knob::gutter;

    outputKnob->setBounds(row.removeFromRight(cellW).withHeight(face + Knob::textHeight));
    row.removeFromRight(Knob::gutter);

    Knob::layoutGrid(row.removeFromLeft(cellW * (int)bandKnobs.size()), bandKnobs,
                     (int)bandKnobs.size());
}

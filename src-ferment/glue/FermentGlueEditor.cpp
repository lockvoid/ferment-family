#include "FermentGlueEditor.h"
#include "FermentGlueProcessor.h"

#include <cmath>

namespace T = ferment::theme;

namespace
{
    // Display formatters (match GlueBlue.cpp conventions).
    juce::String fmtThreshold (double v) { return juce::String((v - 1.0) * 40.0, 1) + " dB"; }
    juce::String fmtRatio     (double v) {
        return juce::String(v < 0.33 ? "2:1" : v < 0.67 ? "4:1" : "10:1");
    }
    juce::String fmtAttack    (double v) { return juce::String(0.03 * std::pow(1000.0, v), 2) + " ms"; }
    juce::String fmtRelease   (double v) {
        if (v >= 0.9) return "Auto";
        return juce::String(0.1 * std::pow(12.0, v / 0.9), 2) + " s";
    }
    juce::String fmtMakeup    (double v) { return juce::String(v * 24.0, 1) + " dB"; }
    juce::String fmtRange     (double v) { return juce::String(v * 60.0, 0) + " dB"; }
    juce::String fmtDryWet    (double v) { return juce::String((int)std::round(v * 100.0)) + "%"; }
    juce::String fmtOnOff     (double v) { return v >= 0.5 ? "On" : "Off"; }
    juce::String fmtHpfFreq   (double v) { return juce::String((int)std::round(20.0 * std::pow(25.0, v))) + " Hz"; }

    constexpr int kMeterWidth = 132;
    constexpr int kColumns    = 5;
}

FermentGlueEditor::FermentGlueEditor(FermentGlueProcessor& p)
    : AudioProcessorEditor(&p),
      processor(p),
      // The DSP reports gain reduction as a positive dB figure, so the scale
      // rests at 0 on the right and deflects left to 12 dB down.
      grMeter([&p] { return p.meterGrDb(); },
              ferment::NeedleMeter::Mode::GR,
              { 0.0, 12.0 },
              "GR")
{
    addAndMakeVisible(chassis);
    addAndMakeVisible(header);
    addAndMakeVisible(grMeter);

    addKnob("threshold", "THRESHOLD", fmtThreshold);
    addKnob("ratio",     "RATIO",     fmtRatio);
    addKnob("attack",    "ATTACK",    fmtAttack);
    addKnob("release",   "RELEASE",   fmtRelease);
    addKnob("makeup",    "MAKEUP",    fmtMakeup);
    addKnob("range",     "RANGE",     fmtRange);
    addKnob("drywet",    "DRY/WET",   fmtDryWet);
    addKnob("sidechain", "SC",        fmtOnOff);
    addKnob("schpf",     "SC HPF",    fmtHpfFreq);
    addKnob("softclip",  "CLIP",      fmtOnOff);

    setSize(620, 256);
}

FermentGlueEditor::~FermentGlueEditor() = default;

void FermentGlueEditor::addKnob(const char* paramID, const char* caption,
                                ferment::FermentKnob::Formatter formatter)
{
    knobs.push_back(std::make_unique<ferment::FermentKnob>(
        processor.apvts, paramID, caption, std::move(formatter)));
    addAndMakeVisible(*knobs.back());
}

void FermentGlueEditor::paint(juce::Graphics& g)
{
    g.fillAll(T::bg);
}

void FermentGlueEditor::resized()
{
    auto inner = ferment::ChassisPanel::layoutFrame(getLocalBounds(), chassis, header);

    grMeter.setBounds(inner.removeFromRight(kMeterWidth).reduced(0, 4));
    inner.removeFromRight(12);

    ferment::FermentKnob::layoutGrid(inner, knobs, kColumns);
}

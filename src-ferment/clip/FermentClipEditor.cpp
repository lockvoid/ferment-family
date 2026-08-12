#include "FermentClipEditor.h"
#include "FermentClipProcessor.h"

#include "FermentClip.h"

#include <cmath>

namespace T = ferment::theme;
namespace FC = airwinconsolidated::FermentClip;

namespace
{
    // Formatters mirror FermentClip.cpp's getParameterDisplay so the editor
    // and the host's generic UI always agree.
    juce::String fmtTrim(double v)
    {
        return juce::String(FC::FermentClip::trimDbFromNorm(v), 1) + " dB";
    }
    juce::String fmtCeiling(double v)
    {
        return juce::String(FC::FermentClip::ceilingDbFromNorm(v), 1) + " dB";
    }
    juce::String fmtPercent(double v)
    {
        return juce::String((int)std::round(v * 100.0)) + "%";
    }
    juce::String fmtBias(double v)
    {
        const int pct = (int)std::round(FC::FermentClip::biasFromNorm(v) * 100.0);
        return (pct > 0 ? "+" : "") + juce::String(pct) + "%";
    }
    juce::String fmtOnOff(double v) { return v >= 0.5 ? "On" : "Off"; }

    constexpr int kMeterWidth = 150;
}

FermentClipEditor::FermentClipEditor(FermentClipProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    addAndMakeVisible(chassis);
    addAndMakeVisible(header);

    using Proc = FermentClipProcessor;
    const char* const* ids = Proc::paramIDs();

    addKnob(ids[Proc::kInput],    "INPUT",     fmtTrim);
    addKnob(ids[Proc::kCeiling],  "CEILING",   fmtCeiling);
    addKnob(ids[Proc::kKnee],     "KNEE",      fmtPercent);
    addKnob(ids[Proc::kTilt],     "TILT",      fmtPercent);
    addKnob(ids[Proc::kBias],     "BIAS",      fmtBias);
    addKnob(ids[Proc::kMix],      "MIX",       fmtPercent);
    addKnob(ids[Proc::kOutput],   "OUTPUT",    fmtTrim);
    addKnob(ids[Proc::kAutoGain], "AUTO GAIN", fmtOnOff);
    addKnob(ids[Proc::kDelta],    "DELTA",     fmtOnOff);

    shaveMeter.setJustificationType(juce::Justification::centredRight);
    shaveMeter.setColour(juce::Label::textColourId, T::amber);
    shaveMeter.setFont(T::mono(12.0f));
    header.setSlotComponent(&shaveMeter, kMeterWidth);

    startTimerHz(15);
    setSize(800, 160);
}

FermentClipEditor::~FermentClipEditor() = default;

void FermentClipEditor::addKnob(const char* paramID, const char* caption,
                                ferment::FermentKnob::Formatter formatter)
{
    knobs.push_back(std::make_unique<ferment::FermentKnob>(
        processor.apvts, paramID, caption, std::move(formatter)));
    addAndMakeVisible(*knobs.back());
}

void FermentClipEditor::timerCallback()
{
    // Peak-hold with decay so single transients stay readable.
    const double now = processor.meterShaveDb();
    shaveHold = std::max(now, shaveHold * 0.85);
    shaveMeter.setText("SHAVE " + juce::String(shaveHold, 1) + " dB",
                       juce::dontSendNotification);
}

void FermentClipEditor::paint(juce::Graphics& g)
{
    g.fillAll(T::bg);
}

void FermentClipEditor::resized()
{
    auto inner = ferment::ChassisPanel::layoutFrame(getLocalBounds(), chassis, header);

    ferment::FermentKnob::layoutGrid(inner, knobs, (int)knobs.size());
}

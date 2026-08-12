#include "FermentLimitEditor.h"
#include "FermentLimitProcessor.h"

#include "FermentLimit.h"

#include <cmath>

namespace T = ferment::theme;
namespace FL = airwinconsolidated::FermentLimit;

namespace
{
    juce::String fmtGain(double v)
    {
        return "+" + juce::String(FL::FermentLimit::gainDbFromNorm(v), 1) + " dB";
    }
    juce::String fmtCeiling(double v)
    {
        return juce::String(FL::FermentLimit::ceilingDbFromNorm(v), 1) + " dB";
    }
    juce::String fmtAttack(double v)
    {
        return juce::String(FL::FermentLimit::attackMsFromNorm(v), 0) + " ms";
    }
    juce::String fmtRelease(double v)
    {
        return juce::String(FL::FermentLimit::releaseScaleFromNorm(v), 2) + "x";
    }
    juce::String fmtTrim(double v)
    {
        return juce::String(FL::FermentLimit::trimDbFromNorm(v), 1) + " dB";
    }
    juce::String fmtPercent(double v)
    {
        return juce::String((int)std::round(v * 100.0)) + "%";
    }
    juce::String fmtOnOff(double v) { return v >= 0.5 ? "On" : "Off"; }

    constexpr int kMeterWidth = 130;
}

FermentLimitEditor::FermentLimitEditor(FermentLimitProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    addAndMakeVisible(chassis);
    addAndMakeVisible(header);

    using Proc = FermentLimitProcessor;
    const char* const* ids = Proc::paramIDs();

    addKnob(ids[Proc::kGain],      "GAIN",      fmtGain);
    addKnob(ids[Proc::kCeiling],   "CEILING",   fmtCeiling);
    addKnob(ids[Proc::kAttack],    "ATTACK",    fmtAttack);
    addKnob(ids[Proc::kRelease],   "RELEASE",   fmtRelease);
    addKnob(ids[Proc::kTransLink], "TR LINK",   fmtPercent);
    addKnob(ids[Proc::kTruePeak],  "TRUE PEAK", fmtOnOff);
    addKnob(ids[Proc::kDelta],     "DELTA",     fmtOnOff);
    addKnob(ids[Proc::kOutput],    "OUTPUT",    fmtTrim);

    grMeter.setJustificationType(juce::Justification::centredRight);
    grMeter.setColour(juce::Label::textColourId, T::amber);
    grMeter.setFont(T::mono(12.0f));
    header.setSlotComponent(&grMeter, kMeterWidth);

    startTimerHz(15);
    setSize(720, 160);
}

FermentLimitEditor::~FermentLimitEditor() = default;

void FermentLimitEditor::addKnob(const char* paramID, const char* caption,
                                 ferment::FermentKnob::Formatter formatter)
{
    knobs.push_back(std::make_unique<ferment::FermentKnob>(
        processor.apvts, paramID, caption, std::move(formatter)));
    addAndMakeVisible(*knobs.back());
}

void FermentLimitEditor::timerCallback()
{
    const double now = processor.meterGrDb();
    grHold = std::max(now, grHold * 0.85);
    grMeter.setText("GR " + juce::String(grHold, 1) + " dB",
                    juce::dontSendNotification);
}

void FermentLimitEditor::paint(juce::Graphics& g)
{
    g.fillAll(T::bg);
}

void FermentLimitEditor::resized()
{
    auto inner = ferment::ChassisPanel::layoutFrame(getLocalBounds(), chassis, header);

    ferment::FermentKnob::layoutGrid(inner, knobs, (int)knobs.size());
}

#include "FermentLimitEditor.h"
#include "FermentLimitProcessor.h"
#include "../common/WarmLookAndFeel.h"
#include "../common/WarmPalette.h"

#include "FermentLimit.h"

#include <cmath>

namespace P = ferment::palette;
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
}

FermentLimitEditor::FermentLimitEditor(FermentLimitProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    lnf = std::make_unique<WarmLookAndFeel>();
    setLookAndFeel(lnf.get());

    using Proc = FermentLimitProcessor;
    const char* const* ids = Proc::paramIDs();

    setupKnob(knobs[Proc::kGain],      ids[Proc::kGain],      "GAIN",      fmtGain);
    setupKnob(knobs[Proc::kCeiling],   ids[Proc::kCeiling],   "CEILING",   fmtCeiling);
    setupKnob(knobs[Proc::kAttack],    ids[Proc::kAttack],    "ATTACK",    fmtAttack);
    setupKnob(knobs[Proc::kRelease],   ids[Proc::kRelease],   "RELEASE",   fmtRelease);
    setupKnob(knobs[Proc::kTransLink], ids[Proc::kTransLink], "TR LINK",   fmtPercent);
    setupKnob(knobs[Proc::kTruePeak],  ids[Proc::kTruePeak],  "TRUE PEAK", fmtOnOff);
    setupKnob(knobs[Proc::kDelta],     ids[Proc::kDelta],     "DELTA",     fmtOnOff);
    setupKnob(knobs[Proc::kOutput],    ids[Proc::kOutput],    "OUTPUT",    fmtTrim);

    grMeter.setJustificationType(juce::Justification::centredRight);
    grMeter.setColour(juce::Label::textColourId, P::amber);
    grMeter.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(),
                               12.0f, juce::Font::plain));
    addAndMakeVisible(grMeter);

    startTimerHz(15);
    setSize(700, 220);
}

FermentLimitEditor::~FermentLimitEditor()
{
    setLookAndFeel(nullptr);
}

void FermentLimitEditor::timerCallback()
{
    const double now = processor.meterGrDb();
    grHold = std::max(now, grHold * 0.85);
    grMeter.setText("GR " + juce::String(grHold, 1) + " dB",
                    juce::dontSendNotification);
}

void FermentLimitEditor::setupKnob(KnobWithLabel& k, const char* paramID,
                                   const char* displayName,
                                   std::function<juce::String(double)> displayFn)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    k.slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                 juce::MathConstants<float>::pi * 2.75f,
                                 true);
    addAndMakeVisible(k.slider);

    k.name.setText(displayName, juce::dontSendNotification);
    k.name.setJustificationType(juce::Justification::centred);
    k.name.setColour(juce::Label::textColourId, P::labelDim);
    k.name.setFont(juce::Font(10.0f, juce::Font::bold));
    addAndMakeVisible(k.name);

    k.value.setJustificationType(juce::Justification::centred);
    k.value.setColour(juce::Label::textColourId, P::amber);
    k.value.setFont(juce::Font(11.0f));
    addAndMakeVisible(k.value);

    auto update = [&k, displayFn]() {
        k.value.setText(displayFn(k.slider.getValue()), juce::dontSendNotification);
    };
    k.slider.onValueChange = update;

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, paramID, k.slider);
    update();
}

void FermentLimitEditor::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    juce::ColourGradient bg(P::panel, b.getCentreX(), b.getCentreY() - 40.f,
                            P::chassis, b.getCentreX(), b.getHeight(), true);
    g.setGradientFill(bg);
    g.fillAll();

    g.setColour(P::chassisEdge);
    g.drawRect(b, 2.0f);
    g.setColour(P::panelHi.withAlpha(0.6f));
    g.drawRect(b.reduced(2.0f), 1.0f);

    auto titleArea = b.removeFromTop(50.0f).reduced(12.0f, 10.0f);
    g.setColour(P::separator);
    g.drawLine(titleArea.getX(), titleArea.getBottom() + 6.0f,
               titleArea.getRight(), titleArea.getBottom() + 6.0f, 1.0f);

    g.setColour(P::labelCream);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::bold));
    g.drawText("FERMENT", titleArea.removeFromLeft(100), juce::Justification::left, false);

    g.setColour(P::amber);
    g.setFont(juce::Font(14.0f, juce::Font::plain));
    g.drawText("Limit  /  dual-stage true-peak limiter", titleArea.withTrimmedRight(120),
               juce::Justification::right, false);
}

void FermentLimitEditor::resized()
{
    grMeter.setBounds(getLocalBounds().removeFromTop(44).removeFromRight(130)
                          .reduced(12, 10));

    auto area = getLocalBounds().withTrimmedTop(60).reduced(16, 16);

    const int cols = 8;
    const int gap  = 8;
    const int cellW = (area.getWidth() - (cols - 1) * gap) / cols;
    const int cellH = area.getHeight();
    const int knobSize = juce::jmin(cellW, cellH) - 34;

    for (int i = 0; i < (int)knobs.size(); ++i)
    {
        auto cell = juce::Rectangle<int>(area.getX() + i * (cellW + gap),
                                         area.getY(), cellW, cellH);
        knobs[i].name.setBounds(cell.removeFromTop(14));
        knobs[i].value.setBounds(cell.removeFromBottom(16));
        knobs[i].slider.setBounds(cell.withSizeKeepingCentre(knobSize, knobSize));
    }
}

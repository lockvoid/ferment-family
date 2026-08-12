#include "FermentClipEditor.h"
#include "FermentClipProcessor.h"
#include "../common/WarmLookAndFeel.h"
#include "../common/WarmPalette.h"

#include "FermentClip.h"

#include <cmath>

namespace P = ferment::palette;
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
}

FermentClipEditor::FermentClipEditor(FermentClipProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    lnf = std::make_unique<WarmLookAndFeel>();
    setLookAndFeel(lnf.get());

    using Proc = FermentClipProcessor;
    const char* const* ids = Proc::paramIDs();

    setupKnob(knobs[Proc::kInput],    ids[Proc::kInput],    "INPUT",    fmtTrim);
    setupKnob(knobs[Proc::kCeiling],  ids[Proc::kCeiling],  "CEILING",  fmtCeiling);
    setupKnob(knobs[Proc::kKnee],     ids[Proc::kKnee],     "KNEE",     fmtPercent);
    setupKnob(knobs[Proc::kTilt],     ids[Proc::kTilt],     "TILT",     fmtPercent);
    setupKnob(knobs[Proc::kBias],     ids[Proc::kBias],     "BIAS",     fmtBias);
    setupKnob(knobs[Proc::kMix],      ids[Proc::kMix],      "MIX",      fmtPercent);
    setupKnob(knobs[Proc::kOutput],   ids[Proc::kOutput],   "OUTPUT",   fmtTrim);
    setupKnob(knobs[Proc::kAutoGain], ids[Proc::kAutoGain], "AUTO GAIN", fmtOnOff);
    setupKnob(knobs[Proc::kDelta],    ids[Proc::kDelta],    "DELTA",    fmtOnOff);

    shaveMeter.setJustificationType(juce::Justification::centredRight);
    shaveMeter.setColour(juce::Label::textColourId, P::amber);
    shaveMeter.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(),
                                  12.0f, juce::Font::plain));
    addAndMakeVisible(shaveMeter);

    startTimerHz(15);
    setSize(760, 220);
}

FermentClipEditor::~FermentClipEditor()
{
    setLookAndFeel(nullptr);
}

void FermentClipEditor::timerCallback()
{
    // Peak-hold with decay so single transients stay readable.
    const double now = processor.meterShaveDb();
    shaveHold = std::max(now, shaveHold * 0.85);
    shaveMeter.setText("SHAVE " + juce::String(shaveHold, 1) + " dB",
                       juce::dontSendNotification);
}

void FermentClipEditor::setupKnob(KnobWithLabel& k, const char* paramID,
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

void FermentClipEditor::paint(juce::Graphics& g)
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
    g.drawText("Clip  /  ADAA mastering clipper", titleArea.withTrimmedRight(140),
               juce::Justification::right, false);
}

void FermentClipEditor::resized()
{
    shaveMeter.setBounds(getLocalBounds().removeFromTop(44).removeFromRight(150)
                             .reduced(12, 10));

    auto area = getLocalBounds().withTrimmedTop(60).reduced(16, 16);

    const int cols = 9;
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

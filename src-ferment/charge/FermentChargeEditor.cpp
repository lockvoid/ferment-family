#include "FermentChargeEditor.h"
#include "FermentChargeProcessor.h"
#include "../common/WarmLookAndFeel.h"
#include "../common/WarmPalette.h"

#include "FermentCharge.h"

#include <cmath>

namespace P = ferment::palette;
namespace FC = airwinconsolidated::FermentCharge;

namespace
{
    // Formatters mirror FermentCharge.cpp's getParameterDisplay so the editor
    // and the host's generic UI always agree.
    juce::String fmtTrim(double v)
    {
        return juce::String(FC::FermentCharge::trimDbFromNorm(v), 1) + " dB";
    }
    juce::String fmtKnob(double v)
    {
        return juce::String(FC::FermentCharge::knobFromNorm(v), 2);
    }
    juce::String fmtPercent(double v)
    {
        return juce::String((int)std::round(v * 100.0)) + "%";
    }
    juce::String fmtOnOff(double v) { return v >= 0.5 ? "On" : "Off"; }

    template <int N>
    juce::String fmtMode(double v, const char* const (&names)[N])
    {
        return names[FC::FermentCharge::modeFromNorm(v, N)];
    }

    const char* const kSatNames[]    = { "Mild", "Moderate", "Hot" };
    const char* const kCharNames[]   = { "Fat", "Warm", "Bright" };
    const char* const kHpNames[]     = { "Off", "100 Hz", "300 Hz" };
    const char* const kStereoNames[] = { "Link", "Dual", "M/S" };
}

FermentChargeEditor::FermentChargeEditor(FermentChargeProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    lnf = std::make_unique<WarmLookAndFeel>();
    setLookAndFeel(lnf.get());

    using Proc = FermentChargeProcessor;
    const char* const* ids = Proc::paramIDs();

    setupKnob(knobs[Proc::kInput],       ids[Proc::kInput],       "INPUT",     fmtTrim);
    setupKnob(knobs[Proc::kCompression], ids[Proc::kCompression], "COMPRESS",  fmtKnob);
    setupKnob(knobs[Proc::kAttack],      ids[Proc::kAttack],      "ATTACK",    fmtKnob);
    setupKnob(knobs[Proc::kRelease],     ids[Proc::kRelease],     "RELEASE",   fmtKnob);
    setupKnob(knobs[Proc::kSaturation],  ids[Proc::kSaturation],  "SAT",       fmtKnob);
    setupKnob(knobs[Proc::kSatMode],     ids[Proc::kSatMode],     "SAT MODE",
              [](double v) { return fmtMode(v, kSatNames); });
    setupKnob(knobs[Proc::kCharacter],   ids[Proc::kCharacter],   "CHARACTER", fmtKnob);
    setupKnob(knobs[Proc::kCharMode],    ids[Proc::kCharMode],    "CHR MODE",
              [](double v) { return fmtMode(v, kCharNames); });
    setupKnob(knobs[Proc::kDetectorHP],  ids[Proc::kDetectorHP],  "DET HP",
              [](double v) { return fmtMode(v, kHpNames); });
    setupKnob(knobs[Proc::kStereoMode],  ids[Proc::kStereoMode],  "STEREO",
              [](double v) { return fmtMode(v, kStereoNames); });
    setupKnob(knobs[Proc::kSidechain],   ids[Proc::kSidechain],   "SC",        fmtOnOff);
    setupKnob(knobs[Proc::kScGain],      ids[Proc::kScGain],      "SC GAIN",   fmtTrim);
    setupKnob(knobs[Proc::kMix],         ids[Proc::kMix],         "MIX",       fmtPercent);
    setupKnob(knobs[Proc::kOutput],      ids[Proc::kOutput],      "OUTPUT",    fmtTrim);

    setSize(840, 320);
}

FermentChargeEditor::~FermentChargeEditor()
{
    setLookAndFeel(nullptr);
}

void FermentChargeEditor::setupKnob(KnobWithLabel& k, const char* paramID,
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

void FermentChargeEditor::paint(juce::Graphics& g)
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
    g.drawText("Charge  /  levelling comp + saturation", titleArea,
               juce::Justification::right, false);
}

void FermentChargeEditor::resized()
{
    auto area = getLocalBounds().withTrimmedTop(60).reduced(16, 16);

    const int cols = 7;
    const int rows = 2;
    const int gap  = 8;

    const int cellW = (area.getWidth()  - (cols - 1) * gap) / cols;
    const int cellH = (area.getHeight() - (rows - 1) * gap) / rows;
    const int knobSize = juce::jmin(cellW, cellH) - 34;

    for (int i = 0; i < (int)knobs.size(); ++i)
    {
        const int col = i % cols;
        const int row = i / cols;
        auto cell = juce::Rectangle<int>(area.getX() + col * (cellW + gap),
                                         area.getY() + row * (cellH + gap),
                                         cellW, cellH);

        knobs[i].name.setBounds(cell.removeFromTop(14));
        knobs[i].value.setBounds(cell.removeFromBottom(16));
        knobs[i].slider.setBounds(cell.withSizeKeepingCentre(knobSize, knobSize));
    }
}

#include "FermentChargeEditor.h"
#include "FermentChargeProcessor.h"

#include "FermentCharge.h"

#include <cmath>

namespace T = ferment::theme;
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

    constexpr int kColumns     = 7;
    constexpr int kMeterWidth  = 132;
}

FermentChargeEditor::FermentChargeEditor(FermentChargeProcessor& p)
    : AudioProcessorEditor(&p),
      processor(p),
      // Charge rides gain slowly and continuously, so the needle is the only
      // thing on the panel that says how hard it is working — the knobs are
      // abstract 1..10, not thresholds in dB.
      grMeter([&p] { return p.meterGrDb(); },
              ferment::NeedleMeter::Mode::GR,
              { 0.0, 12.0 },
              "GR")
{
    addAndMakeVisible(chassis);
    addAndMakeVisible(header);
    addAndMakeVisible(grMeter);

    using Proc = FermentChargeProcessor;
    const char* const* ids = Proc::paramIDs();

    addKnob(ids[Proc::kInput],       "INPUT",     fmtTrim);
    addKnob(ids[Proc::kCompression], "COMPRESS",  fmtKnob);
    addKnob(ids[Proc::kAttack],      "ATTACK",    fmtKnob);
    addKnob(ids[Proc::kRelease],     "RELEASE",   fmtKnob);
    addKnob(ids[Proc::kSaturation],  "SAT",       fmtKnob);
    addKnob(ids[Proc::kSatMode],     "SAT MODE",
            [](double v) { return fmtMode(v, kSatNames); });
    addKnob(ids[Proc::kCharacter],   "CHARACTER", fmtKnob);
    addKnob(ids[Proc::kCharMode],    "CHR MODE",
            [](double v) { return fmtMode(v, kCharNames); });
    addKnob(ids[Proc::kDetectorHP],  "DET HP",
            [](double v) { return fmtMode(v, kHpNames); });
    addKnob(ids[Proc::kStereoMode],  "STEREO",
            [](double v) { return fmtMode(v, kStereoNames); });
    addKnob(ids[Proc::kSidechain],   "SC",        fmtOnOff);
    addKnob(ids[Proc::kScGain],      "SC GAIN",   fmtTrim);
    addKnob(ids[Proc::kMix],         "MIX",       fmtPercent);
    addKnob(ids[Proc::kOutput],      "OUTPUT",    fmtTrim);

    setSize(780, 256);
}

FermentChargeEditor::~FermentChargeEditor() = default;

void FermentChargeEditor::addKnob(const char* paramID, const char* caption,
                                  ferment::FermentKnob::Formatter formatter)
{
    knobs.push_back(std::make_unique<ferment::FermentKnob>(
        processor.apvts, paramID, caption, std::move(formatter)));
    addAndMakeVisible(*knobs.back());
}

void FermentChargeEditor::paint(juce::Graphics& g)
{
    g.fillAll(T::bg);
}

void FermentChargeEditor::resized()
{
    auto inner = ferment::ChassisPanel::layoutFrame(getLocalBounds(), chassis, header);

    grMeter.setBounds(inner.removeFromRight(kMeterWidth).reduced(0, 4));
    inner.removeFromRight(12);

    ferment::FermentKnob::layoutGrid(inner, knobs, kColumns);
}

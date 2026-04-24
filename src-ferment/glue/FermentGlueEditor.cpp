#include "FermentGlueEditor.h"
#include "FermentGlueProcessor.h"

#include <cmath>

namespace
{
    // ---- Warm analog palette ----
    const juce::Colour kChassis       { 0xff1c1410 }; // deep brown-black
    const juce::Colour kChassisEdge   { 0xff0f0a07 }; // edge shadow
    const juce::Colour kPanel         { 0xff2a201a }; // slightly raised panel
    const juce::Colour kPanelHi       { 0xff332820 }; // highlight edge
    const juce::Colour kKnobBody      { 0xff3a2f25 };
    const juce::Colour kKnobBodyDark  { 0xff1f1712 };
    const juce::Colour kKnobRim       { 0xff4a3a2d };
    const juce::Colour kAmber         { 0xffe0a040 }; // tube-glow indicator
    const juce::Colour kAmberDim      { 0xff8a5a20 };
    const juce::Colour kLabelCream    { 0xffd4c4a8 }; // aged ivory
    const juce::Colour kLabelDim      { 0xff7a6a50 };
    const juce::Colour kSeparator     { 0xff4a3828 };
}

// =============================================================================
//  WarmLookAndFeel  —  classic rotary knob with amber indicator line,
//                      brass rim, small cream value readout.
// =============================================================================
class FermentGlueEditor::WarmLookAndFeel : public juce::LookAndFeel_V4
{
public:
    WarmLookAndFeel()
    {
        setColour(juce::Slider::rotarySliderFillColourId,    kAmber);
        setColour(juce::Slider::rotarySliderOutlineColourId, kKnobRim);
        setColour(juce::Label::textColourId,                 kLabelCream);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float rotaryStart, float rotaryEnd,
                          juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(3.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.45f;
        const auto  centre = bounds.getCentre();
        const float angle  = rotaryStart + sliderPos * (rotaryEnd - rotaryStart);

        // --- Outer rim (brass) ---
        g.setColour(kKnobRim.darker(0.25f));
        g.fillEllipse(centre.x - radius - 2, centre.y - radius - 2,
                      (radius + 2) * 2, (radius + 2) * 2);

        // --- Knob body with slight radial gradient for depth ---
        juce::ColourGradient body(kKnobBody,      centre.x, centre.y - radius * 0.4f,
                                  kKnobBodyDark,  centre.x, centre.y + radius * 0.8f, false);
        g.setGradientFill(body);
        g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2);

        // --- Inner shadow ring ---
        g.setColour(kChassisEdge.withAlpha(0.6f));
        g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2, 1.0f);

        // --- Arc showing value (amber glow, subtle) ---
        {
            juce::Path arc;
            arc.addCentredArc(centre.x, centre.y, radius + 1.5f, radius + 1.5f,
                              0.0f, rotaryStart, angle, true);
            g.setColour(kAmber.withAlpha(0.35f));
            g.strokePath(arc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
        }

        // --- Indicator line (amber, center → rim) ---
        juce::Path indicator;
        const float innerR = radius * 0.25f;
        const float outerR = radius * 0.85f;
        indicator.startNewSubPath(0.0f, -outerR);
        indicator.lineTo(0.0f, -innerR);
        g.setColour(kAmber);
        g.strokePath(indicator,
                     juce::PathStrokeType(2.5f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded),
                     juce::AffineTransform::rotation(angle).translated(centre));
    }

    juce::Font getLabelFont(juce::Label& l) override
    {
        auto f = juce::LookAndFeel_V4::getLabelFont(l);
        f.setHeight(11.0f);
        return f;
    }
};

// =============================================================================
//  Editor
// =============================================================================
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
}

FermentGlueEditor::FermentGlueEditor(FermentGlueProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    lnf = std::make_unique<WarmLookAndFeel>();
    setLookAndFeel(lnf.get());

    setupKnob(knobs[0], "threshold", "THRESHOLD", fmtThreshold);
    setupKnob(knobs[1], "ratio",     "RATIO",     fmtRatio);
    setupKnob(knobs[2], "attack",    "ATTACK",    fmtAttack);
    setupKnob(knobs[3], "release",   "RELEASE",   fmtRelease);
    setupKnob(knobs[4], "makeup",    "MAKEUP",    fmtMakeup);
    setupKnob(knobs[5], "range",     "RANGE",     fmtRange);
    setupKnob(knobs[6], "drywet",    "DRY/WET",   fmtDryWet);
    setupKnob(knobs[7], "sidechain", "SC",        fmtOnOff);
    setupKnob(knobs[8], "schpf",     "SC HPF",    fmtHpfFreq);
    setupKnob(knobs[9], "softclip",  "CLIP",      fmtOnOff);

    setSize(720, 320);
}

FermentGlueEditor::~FermentGlueEditor()
{
    setLookAndFeel(nullptr);
}

void FermentGlueEditor::setupKnob(KnobWithLabel& k, const char* paramID, const char* displayName,
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
    k.name.setColour(juce::Label::textColourId, kLabelDim);
    k.name.setFont(juce::Font(10.0f, juce::Font::bold));
    addAndMakeVisible(k.name);

    k.value.setJustificationType(juce::Justification::centred);
    k.value.setColour(juce::Label::textColourId, kAmber);
    k.value.setFont(juce::Font(11.0f));
    addAndMakeVisible(k.value);

    // Live value update — cheap, no listener needed
    auto update = [&k, displayFn]() {
        k.value.setText(displayFn(k.slider.getValue()), juce::dontSendNotification);
    };
    k.slider.onValueChange = update;

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, paramID, k.slider);
    update();
}

void FermentGlueEditor::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // --- Chassis fill with vignette ---
    juce::ColourGradient bg(kPanel, b.getCentreX(), b.getCentreY() - 40.f,
                            kChassis, b.getCentreX(), b.getHeight(), true);
    g.setGradientFill(bg);
    g.fillAll();

    // --- Outer bevel ---
    g.setColour(kChassisEdge);
    g.drawRect(b, 2.0f);
    g.setColour(kPanelHi.withAlpha(0.6f));
    g.drawRect(b.reduced(2.0f), 1.0f);

    // --- Title stripe ---
    auto titleArea = b.removeFromTop(50.0f).reduced(12.0f, 10.0f);
    g.setColour(kSeparator);
    g.drawLine(titleArea.getX(), titleArea.getBottom() + 6.0f,
               titleArea.getRight(), titleArea.getBottom() + 6.0f, 1.0f);

    // Brand wordmark (left)
    g.setColour(kLabelCream);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::bold));
    g.drawText("FERMENT", titleArea.removeFromLeft(100), juce::Justification::left, false);

    // Product name (right, amber)
    g.setColour(kAmber);
    g.setFont(juce::Font(14.0f, juce::Font::plain));
    g.drawText("Glue  /  SSL-style bus comp", titleArea,
               juce::Justification::right, false);
}

void FermentGlueEditor::resized()
{
    auto area = getLocalBounds().withTrimmedTop(60).reduced(16, 16);

    const int cols = 5;
    const int rows = 2;
    const int gap  = 8;

    const int cellW = (area.getWidth()  - (cols - 1) * gap) / cols;
    const int cellH = (area.getHeight() - (rows - 1) * gap) / rows;
    const int knobSize = juce::jmin(cellW, cellH) - 34; // leave room for labels

    for (int i = 0; i < (int)knobs.size(); ++i)
    {
        const int col = i % cols;
        const int row = i / cols;
        const int x = area.getX() + col * (cellW + gap);
        const int y = area.getY() + row * (cellH + gap);

        auto cell = juce::Rectangle<int>(x, y, cellW, cellH);

        auto labelTop = cell.removeFromTop(14);
        knobs[i].name.setBounds(labelTop);

        auto labelBot = cell.removeFromBottom(16);
        knobs[i].value.setBounds(labelBot);

        // Center knob in remaining space
        auto knobArea = cell.withSizeKeepingCentre(knobSize, knobSize);
        knobs[i].slider.setBounds(knobArea);
    }
}

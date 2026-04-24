#include "FermentEqEditor.h"
#include "FermentEqProcessor.h"
#include "../common/WarmLookAndFeel.h"
#include "../common/WarmPalette.h"
#include "../common/Biquad.h"

#include <cmath>

namespace P = ferment::palette;

FermentEqEditor::FermentEqEditor(FermentEqProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    lnf = std::make_unique<WarmLookAndFeel>();
    setLookAndFeel(lnf.get());

    for (int b = 0; b < (int)bandRows.size(); ++b) setupBand(b);

    // Output knob
    outputKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    outputKnob.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    outputKnob.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible(outputKnob);

    outputName.setText("OUTPUT", juce::dontSendNotification);
    outputName.setJustificationType(juce::Justification::centred);
    outputName.setColour(juce::Label::textColourId, P::labelDim);
    outputName.setFont(juce::Font(10.0f, juce::Font::bold));
    addAndMakeVisible(outputName);

    outputVal.setJustificationType(juce::Justification::centred);
    outputVal.setColour(juce::Label::textColourId, P::amber);
    outputVal.setFont(juce::Font(11.0f));
    addAndMakeVisible(outputVal);

    auto updateOut = [this]() {
        const double db = FermentEqProcessor::outputFromNorm(outputKnob.getValue());
        outputVal.setText(juce::String(db, 1) + " dB", juce::dontSendNotification);
    };
    outputKnob.onValueChange = updateOut;
    outputAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "output", outputKnob);
    updateOut();

    setSize(900, 620);
    startTimerHz(30);
}

FermentEqEditor::~FermentEqEditor() { setLookAndFeel(nullptr); }

void FermentEqEditor::setupBand(int b)
{
    auto& row = bandRows[b];

    row.numLabel.setText(juce::String(b + 1), juce::dontSendNotification);
    row.numLabel.setJustificationType(juce::Justification::centred);
    row.numLabel.setColour(juce::Label::textColourId, P::amber);
    row.numLabel.setFont(juce::Font(15.0f, juce::Font::bold));
    addAndMakeVisible(row.numLabel);

    row.typeBox.addItem("Bell",        FermentEqProcessor::Bell      + 1);
    row.typeBox.addItem("Low Shelf",   FermentEqProcessor::LowShelf  + 1);
    row.typeBox.addItem("High Shelf",  FermentEqProcessor::HighShelf + 1);
    row.typeBox.addItem("Low Cut",     FermentEqProcessor::LowCut    + 1);
    row.typeBox.addItem("High Cut",    FermentEqProcessor::HighCut   + 1);
    row.typeBox.addItem("Notch",       FermentEqProcessor::Notch     + 1);
    addAndMakeVisible(row.typeBox);
    row.typeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, FermentEqProcessor::paramId(b, "type"), row.typeBox);

    auto setupKnob = [this](juce::Slider& s, juce::Label& v, juce::Colour vc = P::amber) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        s.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                              juce::MathConstants<float>::pi * 2.75f, true);
        addAndMakeVisible(s);
        v.setJustificationType(juce::Justification::centred);
        v.setColour(juce::Label::textColourId, vc);
        v.setFont(juce::Font(10.5f));
        addAndMakeVisible(v);
    };

    setupKnob(row.freq, row.freqVal);
    setupKnob(row.gain, row.gainVal);
    setupKnob(row.q,    row.qVal);

    auto freqFmt = [](double n) {
        const double f = FermentEqProcessor::freqFromNorm(n);
        if (f >= 1000.0) return juce::String(f / 1000.0, 2) + " k";
        return juce::String((int)std::round(f)) + " Hz";
    };
    auto gainFmt = [](double n) {
        return juce::String(FermentEqProcessor::gainFromNorm(n), 1) + " dB";
    };
    auto qFmt = [](double n) {
        return juce::String(FermentEqProcessor::qFromNorm(n), 2);
    };

    row.freq.onValueChange = [&row, freqFmt]() { row.freqVal.setText(freqFmt(row.freq.getValue()), juce::dontSendNotification); };
    row.gain.onValueChange = [&row, gainFmt]() { row.gainVal.setText(gainFmt(row.gain.getValue()), juce::dontSendNotification); };
    row.q.onValueChange    = [&row, qFmt]()    { row.qVal.setText(qFmt(row.q.getValue()),           juce::dontSendNotification); };

    row.freqAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, FermentEqProcessor::paramId(b, "freq"), row.freq);
    row.gainAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, FermentEqProcessor::paramId(b, "gain"), row.gain);
    row.qAtt    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, FermentEqProcessor::paramId(b, "q"),    row.q);

    row.onBtn.setClickingTogglesState(true);
    row.onBtn.setColour(juce::TextButton::buttonColourId,   P::knobBodyDark);
    row.onBtn.setColour(juce::TextButton::buttonOnColourId, P::amber);
    addAndMakeVisible(row.onBtn);
    row.onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, FermentEqProcessor::paramId(b, "on"), row.onBtn);

    // Trigger initial label update
    row.freq.onValueChange();
    row.gain.onValueChange();
    row.q.onValueChange();
}

void FermentEqEditor::timerCallback()
{
    repaint(graphArea);
}

void FermentEqEditor::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // Chassis + vignette
    juce::ColourGradient bg(P::panel, b.getCentreX(), b.getCentreY() - 40.f,
                            P::chassis, b.getCentreX(), b.getHeight(), true);
    g.setGradientFill(bg);
    g.fillAll();
    g.setColour(P::chassisEdge);
    g.drawRect(b, 2.0f);
    g.setColour(P::panelHi.withAlpha(0.6f));
    g.drawRect(b.reduced(2.0f), 1.0f);

    // Title stripe
    auto titleArea = b.removeFromTop(50.0f).reduced(14.0f, 10.0f);
    g.setColour(P::separator);
    g.drawLine(titleArea.getX(), titleArea.getBottom() + 6.0f,
               titleArea.getRight(), titleArea.getBottom() + 6.0f, 1.0f);

    g.setColour(P::labelCream);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::bold));
    g.drawText("FERMENT", titleArea.removeFromLeft(100), juce::Justification::left, false);

    g.setColour(P::amber);
    g.setFont(juce::Font(14.0f, juce::Font::plain));
    g.drawText("EQ  /  8-band parametric", titleArea, juce::Justification::right, false);

    // Graph
    paintResponse(g);
}

void FermentEqEditor::paintResponse(juce::Graphics& g)
{
    if (graphArea.isEmpty()) return;

    // Background
    g.setColour(P::chassis.darker(0.3f));
    g.fillRect(graphArea);
    g.setColour(P::separator);
    g.drawRect(graphArea, 1);

    auto r = graphArea.reduced(1);
    const double sr = 48000.0; // display at 48k (independent of host SR — RBJ responses are in freq-domain)
    const double xMinHz = 20.0;
    const double xMaxHz = 20000.0;
    const double yRangeDb = 24.0; // ±12 dB
    const int    numPts  = r.getWidth();

    // dB grid (every 6 dB)
    g.setColour(P::graphGrid);
    for (int db = -12; db <= 12; db += 6)
    {
        const float y = (float)r.getY() + (float)r.getHeight() * (float)((yRangeDb * 0.5 - db) / yRangeDb);
        g.drawLine((float)r.getX(), y, (float)r.getRight(), y, db == 0 ? 1.2f : 0.6f);
    }
    // Freq grid (decades + 100/1k/10k ticks)
    const double freqTicks[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    for (double f : freqTicks)
    {
        const double t = std::log(f / xMinHz) / std::log(xMaxHz / xMinHz);
        const float x = (float)r.getX() + (float)r.getWidth() * (float)t;
        g.drawLine(x, (float)r.getY(), x, (float)r.getBottom(), 0.5f);
    }

    // Combined response polyline
    juce::Path path;
    bool started = false;

    // Compute band biquads from current param snapshots
    std::array<ferment::Biquad, FermentEqProcessor::kNumBands> bq;
    std::array<bool, FermentEqProcessor::kNumBands> enabled{};
    for (int i = 0; i < FermentEqProcessor::kNumBands; ++i)
    {
        auto s = processor.snapshot(i);
        enabled[i] = s.enabled;
        const double f = juce::jlimit(20.0, sr * 0.49, s.freq);
        switch (s.type)
        {
            case FermentEqProcessor::Bell:      bq[i].setBell(f, s.q, s.gain, sr); break;
            case FermentEqProcessor::LowShelf:  bq[i].setLowShelf(f, s.q, s.gain, sr); break;
            case FermentEqProcessor::HighShelf: bq[i].setHighShelf(f, s.q, s.gain, sr); break;
            case FermentEqProcessor::LowCut:    bq[i].setLowCut(f, s.q, sr); break;
            case FermentEqProcessor::HighCut:   bq[i].setHighCut(f, s.q, sr); break;
            case FermentEqProcessor::Notch:     bq[i].setNotch(f, s.q, sr); break;
            default:                            bq[i].setIdentity(); break;
        }
    }

    for (int px = 0; px < numPts; ++px)
    {
        const double t = (double)px / (double)(numPts - 1);
        const double freq = xMinHz * std::pow(xMaxHz / xMinHz, t);
        double totalDb = 0.0;
        for (int i = 0; i < FermentEqProcessor::kNumBands; ++i)
            if (enabled[i]) totalDb += bq[i].responseDb(freq, sr);

        totalDb = juce::jlimit(-yRangeDb * 0.5, yRangeDb * 0.5, totalDb);
        const float x = (float)r.getX() + (float)px;
        const float y = (float)r.getY() + (float)r.getHeight() * (float)((yRangeDb * 0.5 - totalDb) / yRangeDb);
        if (!started) { path.startNewSubPath(x, y); started = true; }
        else           path.lineTo(x, y);
    }

    // Fill under curve (subtle)
    juce::Path fill = path;
    fill.lineTo((float)r.getRight(), (float)r.getCentreY());
    fill.lineTo((float)r.getX(),     (float)r.getCentreY());
    fill.closeSubPath();
    g.setColour(P::graphFill);
    g.fillPath(fill);

    // Curve
    g.setColour(P::graphLine);
    g.strokePath(path, juce::PathStrokeType(1.5f));
}

void FermentEqEditor::resized()
{
    auto b = getLocalBounds();
    b.removeFromTop(50);            // title
    auto right = b.removeFromRight(100);

    // Graph area (top)
    graphArea = b.removeFromTop(180).reduced(14, 10);

    // Output knob (right column, over band rows)
    {
        auto area = right.reduced(10);
        area.removeFromTop(20);
        outputName.setBounds(area.removeFromTop(14));
        auto knobArea = area.removeFromTop(70).withSizeKeepingCentre(60, 60);
        outputKnob.setBounds(knobArea);
        outputVal.setBounds(area.removeFromTop(16));
    }

    // Band rows
    auto rowsArea = b.reduced(14, 6);
    const int rowH = rowsArea.getHeight() / (int)bandRows.size();
    for (int i = 0; i < (int)bandRows.size(); ++i)
    {
        auto row = rowsArea.removeFromTop(rowH).reduced(2, 3);

        // Layout within row
        auto num   = row.removeFromLeft(28);
        auto type  = row.removeFromLeft(94);
        row.removeFromLeft(6);
        auto onBtn = row.removeFromRight(44);
        row.removeFromRight(6);

        // Remaining area = 3 knobs in a row
        const int knobCellW = row.getWidth() / 3;
        auto freqCell = row.removeFromLeft(knobCellW);
        auto gainCell = row.removeFromLeft(knobCellW);
        auto qCell    = row;

        auto knobLayout = [](juce::Rectangle<int> cell, juce::Slider& s, juce::Label& v) {
            const int size = juce::jmin(cell.getHeight() - 4, cell.getWidth() / 2);
            auto knobArea = cell.removeFromLeft(size + 8).withSizeKeepingCentre(size, size);
            s.setBounds(knobArea);
            v.setBounds(cell);
        };

        bandRows[i].numLabel.setBounds(num);
        bandRows[i].typeBox.setBounds(type.withSizeKeepingCentre(type.getWidth(), 22));
        knobLayout(freqCell, bandRows[i].freq, bandRows[i].freqVal);
        knobLayout(gainCell, bandRows[i].gain, bandRows[i].gainVal);
        knobLayout(qCell,    bandRows[i].q,    bandRows[i].qVal);
        bandRows[i].onBtn.setBounds(onBtn.withSizeKeepingCentre(onBtn.getWidth(), 22));
    }
}

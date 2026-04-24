#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "WarmPalette.h"

// Warm-analog LookAndFeel shared across Ferment plugins.
// Rotary knob: amber indicator line, brass rim, value arc.
class WarmLookAndFeel : public juce::LookAndFeel_V4
{
public:
    WarmLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float sliderPos, float rotaryStart, float rotaryEnd,
                          juce::Slider&) override;

    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;

    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

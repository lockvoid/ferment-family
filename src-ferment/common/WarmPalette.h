#pragma once
#include <juce_graphics/juce_graphics.h>

// Warm analog palette shared across Ferment plugins.
namespace ferment::palette
{
    const juce::Colour chassis       { 0xff1c1410 }; // deep brown-black chassis
    const juce::Colour chassisEdge   { 0xff0f0a07 };
    const juce::Colour panel         { 0xff2a201a };
    const juce::Colour panelHi       { 0xff332820 };
    const juce::Colour knobBody      { 0xff3a2f25 };
    const juce::Colour knobBodyDark  { 0xff1f1712 };
    const juce::Colour knobRim       { 0xff4a3a2d };
    const juce::Colour amber         { 0xffe0a040 }; // tube-glow indicator
    const juce::Colour amberDim      { 0xff8a5a20 };
    const juce::Colour labelCream    { 0xffd4c4a8 }; // aged ivory
    const juce::Colour labelDim      { 0xff7a6a50 };
    const juce::Colour separator     { 0xff4a3828 };
    const juce::Colour graphGrid     { 0xff3a2e22 };
    const juce::Colour graphLine     { 0xffe0a040 };
    const juce::Colour graphFill     { 0x22e0a040 };
}

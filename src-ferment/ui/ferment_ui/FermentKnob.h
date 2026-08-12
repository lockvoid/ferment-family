#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "FermentTheme.h"

#include <functional>
#include <memory>
#include <vector>

namespace ferment
{

/** The family's rotary control: an amber value arc over a dark track, a dark
    cap with an amber tick, and the value and caption stacked underneath.

    Geometry is expressed as ratios taken from the brand mock (arc r30, arc
    stroke 5.5, cap r21, tick from r7 to r18) so the knob is proportionally
    identical at any size and at any display scale.

    The APVTS attachment is owned internally — construct one and it is live.
*/
class FermentKnob : public juce::Component
{
public:
    /** Receives the slider's current value.  Every Ferment parameter is
        declared over 0..1 (the DSP's own normalisation), so this is the
        normalised value — the same argument the per-editor setupKnob()
        formatters took before this component replaced them. */
    using Formatter = std::function<juce::String (double)>;

    FermentKnob (juce::AudioProcessorValueTreeState& apvts,
                 const juce::String& paramID,
                 const juce::String& caption,
                 Formatter formatter);
    ~FermentKnob() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** For the rare editor that needs to reach the control itself — a Q knob
        wanting a different drag sensitivity, say.  Do not attach to it. */
    juce::Slider& getSlider() noexcept { return slider; }

    /** Height of the value row and the caption row below the knob face. */
    static constexpr int valueRowHeight   = 14;
    static constexpr int captionRowHeight = 13;
    static constexpr int textHeight       = valueRowHeight + captionRowHeight;

    // ---- family-standard grid -------------------------------------------
    /*  Every Ferment editor lays its knobs out through layoutGrid() rather
        than dividing its own width by its own column count.  Left to
        themselves the six editors produced faces from 51 px to 120 px — the
        knob was whatever size happened to be left over, so the family looked
        like six products instead of one.
    */
    using KnobList = std::vector<std::unique_ptr<FermentKnob>>;

    /** Space between adjacent knob faces. Sits inside the cell, so it reads as
        breathing room rather than as margin. */
    static constexpr int gutter = 20;

    /** The face size the family uses wherever there is room for it. */
    static constexpr int standardFaceSize = 64;

    /** Floor for editors too cramped for the standard.  It is a floor, not a
        guarantee: an area that cannot fit `columns` knobs even at this size
        gets an overflowing grid rather than an illegible one. */
    static constexpr int minFaceSize = 40;

    /** The face size a columns x rows grid gets in `area`: the standard, shrunk
        only if the area genuinely cannot fit it. */
    static int faceSizeFor (juce::Rectangle<int> area, int columns, int rows);

    /** Lays `knobs` out as a centred grid `columns` wide, and returns the face
        size used so an editor can match neighbouring controls to it. */
    static int layoutGrid (juce::Rectangle<int> area, const KnobList& knobs, int columns);

    /** Vertical space between rows of a multi-row grid. */
    static constexpr int rowSpacing = 8;

private:
    class Rotary : public juce::Slider
    {
    public:
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        // JUCE's default drag extent; shift-drag makes the knob ten times finer.
        static constexpr int coarseSensitivity = 250;
        static constexpr int fineSensitivity   = 2500;
    };

    void refreshValueText();

    Rotary slider;
    juce::String caption;
    juce::String valueText;
    Formatter formatter;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FermentKnob)
};

} // namespace ferment

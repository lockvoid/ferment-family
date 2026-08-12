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
    void mouseDown (const juce::MouseEvent&) override;

    /** For the rare editor that needs to reach the control itself — a Q knob
        wanting a different drag sensitivity, say.  Do not attach to it. */
    juce::Slider& getSlider() noexcept { return slider; }

    /** The value string currently printed under the knob.  Feeding this back
        through setValueFromText() must return the knob to where it was, which
        is the property the acceptance test pins. */
    const juce::String& displayText() const noexcept { return valueText; }

    // ---- text entry (UI_SPEC §3.1, §7.1) --------------------------------
    /** Opens the inline editor over the value row: type, Enter commits, Esc
        cancels, Tab commits and moves to the next knob.  A click on the value
        row does this; editors can also drive it directly. */
    void beginTextEntry();

    /** Commits a typed string to the parameter, gestures balanced.  This is
        what the inline editor calls on Enter, and it is the whole of text
        entry — so a test that drives this drives the shipping path.
        Returns false if the string did not parse to anything. */
    bool setValueFromText (const juce::String& text);

    /** Finds the slider value whose formatted text best matches @a text, by
        inverting @a formatter numerically over [lo, hi].

        The formatter is the DSP layer's own display mapping — every Ferment
        knob is built from one — so inverting it is how a typed string reaches
        the same value the face would print, without a second parsing table per
        plugin that could disagree with the first.  Words are matched too, so
        typing "hot" into a mode knob works.

        Returns false when nothing sensible matched, leaving @a valueOut alone.
        Static and pure so it can be tested without a message loop. */
    static bool valueForText (const juce::String& text, const Formatter& formatter,
                              double lo, double hi, double interval, double& valueOut);

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
        Rotary();

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

        /** Arrow-key nudge: steps is ±1, coarse is the shift-arrow variant. */
        std::function<void (int steps, bool coarse)> onNudge;

    private:
        // JUCE's default drag extent; shift-drag makes the knob ten times finer.
        static constexpr int coarseSensitivity = 250;
        static constexpr int fineSensitivity   = 2500;
    };

    /*  A plain TextEditor treats Tab as focus traversal, which lands somewhere
        arbitrary rather than on the next knob in the row. */
    class ValueEditor : public juce::TextEditor
    {
    public:
        std::function<bool()> onTabKey;
        bool keyPressed (const juce::KeyPress&) override;
    };

    void refreshValueText();
    void nudge (int steps, bool coarse);
    void setValueWithGesture (double newValue);
    void commitTextEntry();
    void endTextEntry();
    /** The next FermentKnob in the enclosing editor, wrapping at the end. */
    FermentKnob* nextKnob();

    juce::Rectangle<int> valueRow() const;

    Rotary slider;
    ValueEditor valueEditor;
    juce::String caption;
    juce::String valueText;
    Formatter formatter;

    /*  Resolved once.  Typed values and keyboard nudges are written straight to
        the parameter inside a begin/endChangeGesture pair rather than through
        Slider::setValue, which would notify the host with no gesture around it —
        the unbalanced-gesture bug the EQ wheel handler already had to fix. */
    juce::RangedAudioParameter* param = nullptr;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FermentKnob)
};

} // namespace ferment

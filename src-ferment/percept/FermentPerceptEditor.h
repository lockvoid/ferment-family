#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <ferment_ui/ferment_ui.h>

#include "../core/AnalyzerCore.h"
#include "../core/FermentPolicy.h"

#include <array>

class FermentPerceptProcessor;

/** The Analyzer's face: measurements on the left, what to do about them on the
    right.

    One Timer drives the whole editor.  It takes a single readout per tick and
    hands the same snapshot to every panel, so the verdict chips can never be
    describing a different moment from the meters above them.
*/
class FermentPerceptEditor : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit FermentPerceptEditor(FermentPerceptProcessor&);
    ~FermentPerceptEditor() override;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

    /** The rate the editor polls at, and therefore the resolution of the
        meters' three seconds of history. */
    static constexpr int pollHz = 20;

    /** The hint panel's rows, newline separated.

        This panel is the only place `ferment::policy::translate` reaches a
        screen, and its rows are private state.  The acceptance test reads them
        back to prove the panel prints policy's answer rather than one of its
        own — a local mapping table here would be a silent spec violation, and
        there is no other way to see it from outside. */
    juce::String hintText() const;

private:
    /** Six mini-bars: where the energy actually is. The exact share is a hover
        away; the shape is the thing you read at a glance. */
    class BandShareRow : public juce::Component,
                         public juce::SettableTooltipClient
    {
    public:
        BandShareRow();

        void setShares(const double* shares);

        void paint(juce::Graphics&) override;
        void mouseMove(const juce::MouseEvent&) override;
        void mouseExit(const juce::MouseEvent&) override;

        static constexpr int numBands = 6;

    private:
        juce::Rectangle<int> barArea(int band) const;

        std::array<double, numBands> shares {};
        int hovered = -1;
    };

    /** In Source mode this prints ferment-policy's translation of the current
        readout — the settings Master would apply. In Result mode it prints
        pass/fail against the profile instead. */
    class HintPanel : public juce::Component
    {
    public:
        HintPanel();

        void showChain(const ferment::policy::ChainSettings&);
        void showVerification(const ferment::analyzer::Readout&,
                              const ferment::analyzer::Profile&);

        void setCollapsed(bool);

        /** What is currently on screen, newline separated. */
        juce::String text() const;

        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;

        static constexpr int titleHeight = 18;
        static constexpr int rowHeight   = 15;

    private:
        /*  The raw bytes are kept alongside the decoded string so a row can be
            compared without building anything.  juce::String has no small-string
            optimisation and the separator is a UTF-8 middle dot, so decoding
            first and comparing afterwards put six allocations on every one of
            the twenty polls a second — which the allocation harness duly
            caught. */
        struct Row
        {
            juce::String text;
            char raw[96] = {};
            bool warn = false;
            bool label = false;    ///< a module-name row above its values
        };

        /** Commits a formatted row only when its characters actually changed,
            so a steady readout costs no allocation per poll. */
        void setRow(int index, const char* text, bool warn = false, bool label = false);
        /** A dim caption row — the module name; its values go on the rows below. */
        void setLabel(int index, const char* text) { setRow(index, text, false, true); }
        void trimTo(int count);

        juce::String title { "CHAIN HINT" };
        std::vector<Row> rows;
        bool collapsed = false;
    };

    void timerCallback() override;
    void refreshSourceMode(const ferment::analyzer::Readout&,
                           const ferment::analyzer::Profile&);
    void refreshResultMode(const ferment::analyzer::Readout&,
                           const ferment::analyzer::Profile&);

    /** Wires a row of toggles to a choice parameter through a hidden ComboBox —
        the family's established way of attaching a button bar. */
    void setupBar(juce::ComboBox& box,
                  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& attachment,
                  const juce::String& paramID,
                  const juce::StringArray& items,
                  std::vector<ferment::FermentToggle*> buttons);

    FermentPerceptProcessor& processor;

    ferment::ChassisPanel chassis;
    ferment::HeaderBar    header { "PERCEPT / THE EAR BEFORE THE CHAIN" };

    ferment::MeterStrip   meters { pollHz };
    BandShareRow          bands;
    ferment::VerdictChips chips;
    ferment::TargetList   targets;
    HintPanel             hints;

    ferment::FermentToggle sourceBtn { "SOURCE" }, resultBtn { "RESULT" };
    ferment::FermentToggle studioBtn { "STUDIO" }, reelBtn   { "REEL"   };
    ferment::FermentToggle resetBtn  { "RESET"  };

    /*  Attachments are declared after the ComboBox they attach to: members die
        in reverse declaration order, and ~ComboBoxAttachment calls
        removeListener on its box. The other way round reads freed memory —
        the exact use-after-free that shipped in the Utility editor. */
    juce::ComboBox modeBox, profileBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt, profileAtt;

    juce::TooltipWindow tooltips { this, 500 };

    /*  Section captions are not children, so they still have to be positioned.
        resized() works out where they go and paint() draws them there — the one
        arithmetic, rather than the same layout written out twice and free to
        drift apart the first time a gap changes. */
    juce::Rectangle<int> bandCaption, verdictCaption, targetCaption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentPerceptEditor)
};

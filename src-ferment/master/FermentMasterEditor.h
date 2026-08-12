#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <ferment_ui/ferment_ui.h>

#include "FermentMasterProcessor.h"

#include <array>
#include <memory>
#include <vector>

/** Ferment Master's face: the chain laid out as five cards, and one button that
    sets all of them.

    A single Timer drives everything the face shows — meters and Learn state.
    The write-back of a finished Learn is the processor's, not this window's.
*/
class FermentMasterEditor : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit FermentMasterEditor(FermentMasterProcessor&);
    ~FermentMasterEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int pollHz = 20;

private:
    /** The header's right slot: what came in, and what is going out. */
    class ReadoutSlot : public juce::Component
    {
    public:
        void set(double sourceLufs, double sourceTp, double resultLufs, double resultTp);
        void paint(juce::Graphics&) override;

    private:
        juce::String sourceText { "--" }, resultText { "--" };
    };

    struct Card
    {
        std::unique_ptr<ferment::ModuleCard> card;
        std::unique_ptr<ferment::FermentToggle> bypass;
        std::unique_ptr<ferment::NeedleMeter> meter;
        std::vector<std::unique_ptr<ferment::FermentKnob>> knobs;
        ferment::master::Module module {};
    };

    Card& addCard(ferment::master::Module, const juce::String& title);
    void addKnob(Card&, FermentMasterProcessor::Param,
                 const char* caption, ferment::FermentKnob::Formatter);

    /** The width a card needs to hold its knobs at the family's standard face
        size, with the family's gutter between them. */
    int cardWidth(const Card&) const;

    void timerCallback() override;

    FermentMasterProcessor& processor;

    ferment::HeaderBar header { "MASTER / THE WHOLE CHAIN, ONE LEARN" };
    ReadoutSlot        readouts;

    /*  The chain is one horizontal run of units, scrolled rather than wrapped.
        Wrapping it over two rows forced every knob below the family's standard
        size to make the widest module fit, which is the drift FermentKnob's grid
        exists to stop; a rack you slide sideways keeps every face identical to
        the standalone plugin it stands for.
    */
    juce::Viewport  rack;
    juce::Component rackContent;
    juce::Rectangle<int> rackWell;

    std::vector<std::unique_ptr<Card>> cards;

    ferment::LearnButton   learnButton;
    ferment::FermentToggle studioBtn { "STUDIO" }, reelBtn { "REEL" };

    /*  The attachment is declared after the ComboBox it listens to: reverse
        declaration order at destruction is what turned this exact pattern into
        a use-after-free in the Utility editor. */
    juce::ComboBox profileBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> profileAtt;

    bool learnPending = false;
    int  lastLearnPhase = 0;   ///< so the flash follows the phase 2 -> idle edge

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentMasterEditor)
};

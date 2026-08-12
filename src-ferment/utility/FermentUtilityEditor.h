#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <ferment_ui/ferment_ui.h>

#include <array>
#include <memory>
#include <vector>

class FermentUtilityProcessor;

class FermentUtilityEditor : public juce::AudioProcessorEditor
{
public:
    explicit FermentUtilityEditor(FermentUtilityProcessor&);
    ~FermentUtilityEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void addKnob(const char* paramId, const char* caption,
                 ferment::FermentKnob::Formatter formatter);

    FermentUtilityProcessor& processor;

    ferment::ChassisPanel chassis;
    // As with the phase glyph, the multiplication sign has to be handed over as
    // explicit UTF-8 — juce::String(const char*) reads its input as ASCII.
    ferment::HeaderBar    header { juce::String::fromUTF8 ("UTILITY / ROUTING \xc3\x97 GAIN") };

    // Gain / Bal L / Bal R / Width / Bass Mono Freq
    std::vector<std::unique_ptr<ferment::FermentKnob>> knobs;

    /*  Boolean toggles.  FermentToggle owns its own parameter attachment, so
        these no longer need the careful "attachment declared after the control"
        ordering the bars below still depend on. */
    ferment::FermentToggle muteBtn   { "MUTE"   };
    // juce::String(const char*) reads its input as ASCII, so the phase-invert
    // symbol has to be handed over as explicit UTF-8.
    ferment::FermentToggle phaseLBtn { juce::String::fromUTF8 ("\xc3\xb8 L") };
    ferment::FermentToggle phaseRBtn { juce::String::fromUTF8 ("\xc3\xb8 R") };
    ferment::FermentToggle dcBtn       { "DC"     };
    ferment::FermentToggle bassMonoBtn { "BASS M" };

    /*  Attachments must be declared AFTER the control they attach to: members
        are destroyed in reverse declaration order, and ComboBoxAttachment's
        destructor calls removeListener on its ComboBox.  With the ComboBox
        listed last it was freed first, and closing the editor read freed
        memory (caught by AddressSanitizer).
    */

    // Channel mode as button bar (Stereo / Swap / L / R / Mono)
    std::array<ferment::FermentToggle, 5> chanBtns;
    juce::ComboBox chanHidden;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chanAtt;

    // Mid/Side solo as 3-button bar (Off / Mid / Side)
    std::array<ferment::FermentToggle, 3> msBtns;
    juce::ComboBox msHidden;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> msAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentUtilityEditor)
};

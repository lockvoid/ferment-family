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
    /*  Utility is the one editor with buttons, and the component kit has no
        button in v1.  Rather than keep WarmLookAndFeel alive for this alone,
        the toggles are styled here from the shared theme tokens.  If a second
        editor ever needs them, this is the thing to promote to a proper
        ferment_ui component.
    */
    class ToggleLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        ToggleLookAndFeel();
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                                  bool isHighlighted, bool isDown) override;
        juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    };

    void addKnob(const char* paramId, const char* caption,
                 ferment::FermentKnob::Formatter formatter);
    /** Bar buttons drive a hidden ComboBox from onClick, so their toggle
        state is set for them rather than by the click. */
    void setupToggle(juce::TextButton&, bool clickToggles);

    FermentUtilityProcessor& processor;

    ToggleLookAndFeel toggleLnf;

    ferment::ChassisPanel chassis;
    // As with the phase glyph, the multiplication sign has to be handed over as
    // explicit UTF-8 — juce::String(const char*) reads its input as ASCII.
    ferment::HeaderBar    header { juce::String::fromUTF8 ("UTILITY / ROUTING \xc3\x97 GAIN") };

    // Gain / Bal L / Bal R / Width / Bass Mono Freq
    std::vector<std::unique_ptr<ferment::FermentKnob>> knobs;

    // Boolean toggles
    juce::TextButton muteBtn   { "MUTE"   };
    // juce::String(const char*) reads its input as ASCII, so the phase-invert
    // symbol has to be handed over as explicit UTF-8.
    juce::TextButton phaseLBtn { juce::String::fromUTF8 ("\xc3\xb8 L") };
    juce::TextButton phaseRBtn { juce::String::fromUTF8 ("\xc3\xb8 R") };
    juce::TextButton dcBtn       { "DC"     };
    juce::TextButton bassMonoBtn { "BASS M" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtt, phaseLAtt, phaseRAtt, dcAtt, bassMonoAtt;

    /*  Attachments must be declared AFTER the control they attach to: members
        are destroyed in reverse declaration order, and ComboBoxAttachment's
        destructor calls removeListener on its ComboBox.  With the ComboBox
        listed last it was freed first, and closing the editor read freed
        memory (caught by AddressSanitizer).
    */

    // Channel mode as button bar (Stereo / Swap / L / R / Mono)
    std::array<juce::TextButton, 5> chanBtns;
    juce::ComboBox chanHidden;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chanAtt;

    // Mid/Side solo as 3-button bar (Off / Mid / Side)
    std::array<juce::TextButton, 3> msBtns;
    juce::ComboBox msHidden;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> msAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FermentUtilityEditor)
};

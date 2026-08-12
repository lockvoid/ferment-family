/*  Ferment UI Gallery — every plugin editor in one window.

    A design-review harness, not a product: it opens no audio device and loads
    no host, it just instantiates each processor and shows its real editor.
    The scale selector re-renders at the display scales the UI spec requires
    (1x / 1.25x / 1.5x / 2x), which is the quickest way to catch a component
    that has hard-coded a pixel somewhere.
*/

#include "../src-ferment/charge/FermentChargeProcessor.h"
#include "../src-ferment/clip/FermentClipProcessor.h"
#include "../src-ferment/eq/FermentEqProcessor.h"
#include "../src-ferment/glue/FermentGlueProcessor.h"
#include "../src-ferment/limit/FermentLimitProcessor.h"
#include "../src-ferment/utility/FermentUtilityProcessor.h"

#include "FermentToolAudio.h"

#include <ferment_ui/ferment_ui.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <memory>
#include <vector>

namespace
{
constexpr int   kBarHeight = 34;
constexpr float kScales[]  = { 1.0f, 1.25f, 1.5f, 2.0f };

/** The gallery has no live input, so it primes the current plugin itself.  A
    little under programme level, which keeps the needle off its end stop. */
constexpr float kGalleryLevel = 0.66f;

using ferment::tools::primeWithAudio;

class Gallery : public juce::Component,
                private juce::Timer
{
public:
    Gallery()
    {
        add ("Clip",    std::make_unique<FermentClipProcessor>());
        add ("Limit",   std::make_unique<FermentLimitProcessor>());
        add ("Glue",    std::make_unique<FermentGlueProcessor>());
        add ("Charge",  std::make_unique<FermentChargeProcessor>());
        add ("Utility", std::make_unique<FermentUtilityProcessor>());
        add ("EQ",      std::make_unique<FermentEqProcessor>());

        for (size_t i = 0; i < plugins.size(); ++i)
            pluginBox.addItem (plugins[i].name, (int) i + 1);

        pluginBox.setSelectedItemIndex (0, juce::dontSendNotification);
        pluginBox.onChange = [this] { rebuild(); };
        addAndMakeVisible (pluginBox);

        for (int i = 0; i < (int) std::size (kScales); ++i)
            scaleBox.addItem (juce::String (kScales[i], 2) + "x", i + 1);

        scaleBox.setSelectedItemIndex (0, juce::dontSendNotification);
        scaleBox.onChange = [this] { rebuild(); };
        addAndMakeVisible (scaleBox);

        hint.setText ("drag knobs · shift = fine · double-click = reset · "
                      "EQ: drag nodes, wheel = Q, right-click = menu",
                      juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, ferment::theme::labelDim);
        hint.setFont (ferment::theme::mono (11.0f));
        hint.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (hint);

        addAndMakeVisible (holder);

        rebuild();

        // Keep the meters alive so the needle and analyser have signal.
        startTimerHz (4);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (ferment::theme::bg);
        g.setColour (ferment::theme::faceEdge);
        g.drawHorizontalLine (kBarHeight - 1, 0.0f, (float) getWidth());
    }

    void resized() override
    {
        auto bar = getLocalBounds().removeFromTop (kBarHeight).reduced (8, 5);
        pluginBox.setBounds (bar.removeFromLeft (120));
        bar.removeFromLeft (8);
        scaleBox.setBounds (bar.removeFromLeft (80));
        bar.removeFromLeft (12);
        hint.setBounds (bar);

        holder.setBounds (getLocalBounds().withTrimmedTop (kBarHeight));
    }

    std::function<void (int, int)> onSizeWanted;

private:
    struct Entry
    {
        juce::String name;
        std::unique_ptr<juce::AudioProcessor> processor;
    };

    void add (const juce::String& name, std::unique_ptr<juce::AudioProcessor> p)
    {
        p->setPlayConfigDetails (2, 2, 48000.0, 512);
        p->prepareToPlay (48000.0, 512);
        plugins.push_back ({ name, std::move (p) });
    }

    void timerCallback() override
    {
        if (auto* p = current())
            primeWithAudio (*p, 4, kGalleryLevel);
    }

    juce::AudioProcessor* current()
    {
        const int i = pluginBox.getSelectedItemIndex();
        return juce::isPositiveAndBelow (i, (int) plugins.size())
             ? plugins[(size_t) i].processor.get() : nullptr;
    }

    void rebuild()
    {
        editor.reset();

        auto* p = current();

        if (p == nullptr)
            return;

        primeWithAudio (*p, 48, kGalleryLevel);

        editor.reset (p->createEditor());

        if (editor == nullptr)
            return;

        const float scale = kScales[juce::jlimit (0, (int) std::size (kScales) - 1,
                                                  scaleBox.getSelectedItemIndex())];

        holder.addAndMakeVisible (*editor);
        editor->setTopLeftPosition (0, 0);
        editor->setTransform (juce::AffineTransform::scale (scale));

        const int w = (int) std::round ((float) editor->getWidth()  * scale);
        const int h = (int) std::round ((float) editor->getHeight() * scale);

        if (onSizeWanted)
            onSizeWanted (juce::jmax (w, 620), h + kBarHeight);
    }

    std::vector<Entry> plugins;
    std::unique_ptr<juce::AudioProcessorEditor> editor;

    juce::Component holder;
    juce::ComboBox  pluginBox, scaleBox;
    juce::Label     hint;
};

class GalleryWindow : public juce::DocumentWindow
{
public:
    GalleryWindow()
        : DocumentWindow ("Ferment UI Gallery",
                          juce::Colour (ferment::theme::bg),
                          DocumentWindow::closeButton)
    {
        auto* gallery = new Gallery();
        gallery->onSizeWanted = [this] (int w, int h) { setContentComponentSize (w, h); };

        setUsingNativeTitleBar (true);
        setContentOwned (gallery, true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class GalleryApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Ferment UI Gallery"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override { window = std::make_unique<GalleryWindow>(); }
    void shutdown() override                       { window.reset(); }

private:
    std::unique_ptr<GalleryWindow> window;
};

} // namespace

START_JUCE_APPLICATION (GalleryApp)

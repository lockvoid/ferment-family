/*  Renders every Ferment plugin editor offscreen to PNG at 1x and 2x.

    This is the visual check the UI spec asks for on a migration PR, without
    having to launch six Standalone builds by hand.

        ./ferment_editor_shots <output-dir>
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
#include <set>

namespace
{

using ferment::tools::primeWithAudio;

/** How long to let editor Timers run before the shot. Sized so meter ballistics
    finish converging — see the note in shoot(). */
constexpr int settleMs = 1500;

/** Reports the knob face sizes an editor actually laid out, so drift across the
    family shows up as a number rather than a feeling. */
void reportKnobSizes (juce::Component& editor, const juce::String& name)
{
    std::set<int> sizes;

    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        if (auto* knob = dynamic_cast<ferment::FermentKnob*> (&c))
            sizes.insert (knob->getSlider().getWidth());

        for (auto* child : c.getChildren())
            walk (*child);
    };

    walk (editor);

    juce::String list;
    for (int s : sizes)
        list += (list.isEmpty() ? "" : ", ") + juce::String (s);

    std::printf ("  %-8s knob face: %s px\n", name.toRawUTF8(),
                 list.isEmpty() ? "none" : list.toRawUTF8());
}

void writePng (juce::Component& c, const juce::File& dir, const juce::String& name, float scale)
{
    juce::Image image (juce::Image::ARGB,
                       (int) std::round ((float) c.getWidth()  * scale),
                       (int) std::round ((float) c.getHeight() * scale),
                       true);

    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale (scale));
    c.paintEntireComponent (g, false);

    const auto file = dir.getChildFile (name + (scale > 1.0f ? "@2x.png" : ".png"));
    file.deleteFile();

    juce::FileOutputStream stream (file);
    juce::PNGImageFormat png;
    png.writeImageToStream (image, stream);

    std::printf ("wrote %s (%dx%d)\n", file.getFullPathName().toRawUTF8(),
                 image.getWidth(), image.getHeight());
}

template <typename Processor>
void shoot (const juce::File& dir, const juce::String& name,
            const std::function<void (Processor&)>& setup = {})
{
    Processor processor;
    processor.prepareToPlay (48000.0, 512);

    if (setup)
        setup (processor);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    if (editor == nullptr)
    {
        std::printf ("!! %s has no editor\n", name.toRawUTF8());
        return;
    }

    // Let the editor's Timers run until the meters have actually converged, not
    // merely started moving.  A needle closes on its target exponentially with a
    // 50 ms attack, so reaching the point where the remaining error is smaller
    // than a rendered pixel takes ~50 * ln(target / epsilon) ms — around 800 ms
    // for a 10 dB reading.  Settling for only 400 ms left the needle still
    // creeping, and the shot then depended on exactly how many timer ticks the
    // machine happened to fit in the window: glue.png and charge.png came out
    // byte-different on every run, so screenshots/ churned with no code change.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (settleMs);

    reportKnobSizes (*editor, name);

    for (float scale : { 1.0f, 2.0f })
        writePng (*editor, dir, name, scale);
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String requested = argc > 1 ? juce::String (argv[1]) : juce::String ("screenshots");

    // Absolute paths must not go through getChildFile, which treats its
    // argument as relative — the CMake target passes one.
    const juce::File outDir = juce::File::isAbsolutePath (requested)
                            ? juce::File (requested)
                            : juce::File::getCurrentWorkingDirectory().getChildFile (requested);

    const auto created = outDir.createDirectory();

    if (created.failed())
    {
        std::printf ("!! cannot write to %s: %s\n",
                     outDir.getFullPathName().toRawUTF8(),
                     created.getErrorMessage().toRawUTF8());
        return 1;
    }

    std::printf ("rendering editors to %s\n", outDir.getFullPathName().toRawUTF8());

    shoot<FermentClipProcessor>    (outDir, "clip");
    shoot<FermentLimitProcessor>   (outDir, "limit");
    shoot<FermentGlueProcessor> (outDir, "glue", [] (FermentGlueProcessor& p)
    {
        primeWithAudio (p, 64, 1.6f);
        std::printf ("  glue     GR: %.2f dB\n", p.meterGrDb());
    });
    shoot<FermentChargeProcessor> (outDir, "charge", [] (FermentChargeProcessor& p)
    {
        primeWithAudio (p, 64, 1.6f);
        std::printf ("  charge   GR: %.2f dB\n", p.meterGrDb());
    });
    shoot<FermentUtilityProcessor> (outDir, "utility");

    // Give the EQ a curve worth looking at rather than eight flat bells.
    shoot<FermentEqProcessor> (outDir, "eq", [] (FermentEqProcessor& p)
    {
        auto set = [&p] (const juce::String& id, float norm)
        {
            if (auto* param = p.apvts.getParameter (id))
                param->setValueNotifyingHost (norm);
        };

        set (FermentEqProcessor::paramId (0, "freq"), 0.18f);
        set (FermentEqProcessor::paramId (0, "gain"), 0.68f);
        set (FermentEqProcessor::paramId (1, "freq"), 0.44f);
        set (FermentEqProcessor::paramId (1, "gain"), 0.31f);
        set (FermentEqProcessor::paramId (1, "q"),    0.62f);
        set (FermentEqProcessor::paramId (2, "freq"), 0.72f);
        set (FermentEqProcessor::paramId (2, "gain"), 0.64f);
        set (FermentEqProcessor::paramId (3, "type"), 1.0f);
        set (FermentEqProcessor::paramId (3, "freq"), 0.90f);
        set (FermentEqProcessor::paramId (4, "on"),   0.0f);

        primeWithAudio (p, 64, 1.0f);
    });

    return 0;
}

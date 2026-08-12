#pragma once

/*  Shared by the Ferment dev tools (editor screenshots, UI gallery).

    Both of them show editors with no host attached, so every meter, needle and
    spectrum analyser sits at rest unless something feeds the processor first.
    Two copies of that drifted apart once already; this is the one copy.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

namespace ferment::tools
{

/** Runs `blocks` blocks of pink-ish noise plus three tones through `p`, so its
    meters and analysers have something to show.  `level` scales the lot: 1.0 is
    a comfortable programme level, 1.6 pushes a compressor into real gain
    reduction, 0.6 keeps a gallery needle off the end stop. */
inline void primeWithAudio (juce::AudioProcessor& p, int blocks, float level = 1.0f)
{
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    juce::Random rng (0x5eed);
    double phase[3] = { 0.0, 0.0, 0.0 };
    const double tones[3] = { 110.0, 880.0, 5000.0 };
    float lp = 0.0f;

    for (int block = 0; block < blocks; ++block)
    {
        for (int n = 0; n < buf.getNumSamples(); ++n)
        {
            lp += 0.35f * ((rng.nextFloat() * 2.0f - 1.0f) - lp);
            float v = lp * 0.6f;

            for (int t = 0; t < 3; ++t)
            {
                phase[t] += 2.0 * juce::MathConstants<double>::pi * tones[t] / 48000.0;
                v += 0.18f * (float) std::sin (phase[t]);
            }

            buf.setSample (0, n, v * level);
            buf.setSample (1, n, v * level);
        }

        p.processBlock (buf, midi);
    }
}

} // namespace ferment::tools

#pragma once

#include <juce_graphics/juce_graphics.h>

#include <cmath>

/*  Design tokens for the Ferment plugin family — the single source of truth.

    Every colour and every font in ferment_ui comes from here, and nothing
    else in the module may name a colour.  The values are lifted from the
    brand mock (ferment-landing → KnobRackMock.astro, the "GLUE BLUE" rack);
    adding one is a lead decision, not a component decision.

    Where a component needs a lighting effect rather than a colour — a glass
    highlight, a cast shadow — it uses one of these tokens with an alpha, so
    the palette stays closed.
*/
namespace ferment::theme
{
    // chassis / face
    inline const juce::Colour bg          { 0xFF050505 };  // page black
    inline const juce::Colour faceTop     { 0xFF181210 };  // panel gradient top
    inline const juce::Colour faceBottom  { 0xFF0d0908 };  // panel gradient bottom
    inline const juce::Colour faceEdge    { 0xFF130e0b };

    // the mock's timber, now used for warm greys rather than for actual wood
    inline const juce::Colour woodLight   { 0xFFd9b98a };
    inline const juce::Colour woodDark    { 0xFF3a2c20 };
    inline const juce::Colour woodShadow  { 0xFF2b2118 };

    // accents
    inline const juce::Colour amber       { 0xFFE8902A };  // the ONE accent
    inline const juce::Colour labelDim    { 0x998a7666 };  // knob captions
    inline const juce::Colour labelCream  { 0xFFEFE6D8 };  // FERMENT wordmark

    /*  Typography: monospaced throughout, captions ALL CAPS with ~0.12em
        tracking.  JUCE 8 FontOptions only — the deprecated juce::Font ctors
        are not to be used anywhere in this module.  No bundled fonts in v1,
        so this is whatever the platform calls its default monospace.
    */
    inline constexpr float tracking = 0.12f;

    inline juce::Font mono (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              height,
                                              bold ? juce::Font::bold : juce::Font::plain));
    }

    inline juce::Font monoTracked (float height, bool bold = false,
                                   float kerning = tracking)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              height,
                                              bold ? juce::Font::bold : juce::Font::plain)
                               .withKerningFactor (kerning));
    }

    /*  Corners.

        A circular rounded rect changes curvature instantly where the arc meets
        the straight edge, and the eye reads that discontinuity as "rectangle
        with arcs stuck on".  A squircle — a superellipse corner — ramps the
        curvature in, so the corner looks moulded rather than cut.  JUCE only
        draws circular arcs, so the path is sampled directly from the
        superellipse.

        smoothness 2.0 is exactly a circle; ~4.5 is the continuous-corner look.
    */
    inline constexpr float cornerSmoothness = 4.5f;

    inline juce::Path squircle (juce::Rectangle<float> bounds, float radius,
                                float smoothness = cornerSmoothness)
    {
        juce::Path p;

        if (bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f)
            return p;

        const float r = juce::jlimit (0.0f,
                                      juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f,
                                      radius);

        if (r <= 0.5f)
        {
            p.addRectangle (bounds);
            return p;
        }

        const float exponent = 2.0f / juce::jmax (2.0f, smoothness);
        constexpr int steps = 16;

        const float x0 = bounds.getX(), x1 = bounds.getRight();
        const float y0 = bounds.getY(), y1 = bounds.getBottom();

        /*  std::cos (halfPi) lands a hair BELOW zero in float (-4.4e-8), and
            pow() of a negative base with a fractional exponent is NaN.  That
            put four NaN vertices in every squircle: jassert-storm in a debug
            build, and a corner segment the rasteriser quietly dropped.  The
            sine and cosine of a first-quadrant angle can only be >= 0, so
            clamping there is the whole fix.
        */
        auto axis = [exponent, r] (float v) { return std::pow (juce::jmax (0.0f, v), exponent) * r; };

        // Walks a quarter superellipse around one corner centre.  sineFirst
        // picks which axis leads, which is what orients the sweep.
        auto sweep = [&] (float cx, float cy, float xs, float ys, bool sineFirst)
        {
            for (int i = 0; i <= steps; ++i)
            {
                const float t = (float) i / (float) steps * juce::MathConstants<float>::halfPi;
                const float a = axis (std::sin (t));
                const float b = axis (std::cos (t));

                p.lineTo (cx + xs * (sineFirst ? a : b),
                          cy + ys * (sineFirst ? b : a));
            }
        };

        p.startNewSubPath (x0 + r, y0);
        p.lineTo (x1 - r, y0);
        sweep (x1 - r, y0 + r, 1.0f, -1.0f, true);    // top-right
        p.lineTo (x1, y1 - r);
        sweep (x1 - r, y1 - r, 1.0f, 1.0f, false);    // bottom-right
        p.lineTo (x0 + r, y1);
        sweep (x0 + r, y1 - r, -1.0f, 1.0f, true);    // bottom-left
        p.lineTo (x0, y0 + r);
        sweep (x0 + r, y0 + r, -1.0f, -1.0f, false);  // top-left
        p.closeSubPath();

        return p;
    }
}

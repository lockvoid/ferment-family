#include "NeedleMeter.h"

#include <cmath>

namespace ferment
{

namespace
{
    // Half the needle's total sweep, either side of vertical — a classic VU
    // movement rather than a wide arc.
    constexpr float sweepHalf = 0.663f;   // ~38 degrees

    // Larger than a circular corner would take, because a squircle's
    // curvature ramps in — the same radius reads tighter.
    constexpr float corner = 11.0f;

    /** A tick step that divides the scale into at most five intervals, chosen
        from values that read naturally on a dB scale. */
    double tickStepFor (double span)
    {
        static const double candidates[] = { 0.5, 1.0, 2.0, 3.0, 5.0, 6.0, 10.0, 12.0, 20.0, 30.0 };

        for (double c : candidates)
            if (span / c <= 5.0)
                return c;

        return span * 0.25;
    }

    /** Where the movement sits inside the meter's face. */
    struct Dial
    {
        juce::Rectangle<float> captionRow;
        juce::Point<float> pivot;
        float radius = 0.0f;
        bool valid = false;
    };

    Dial dialFor (juce::Rectangle<float> face, bool hasCaption)
    {
        Dial d;
        auto scaleArea = face;

        if (hasCaption)
            d.captionRow = scaleArea.removeFromBottom (12.0f);

        // The movement spans one radius vertically, so size it against the
        // width first and then centre that band in the face.
        d.radius = juce::jmin ((scaleArea.getWidth() * 0.5f - 6.0f) / std::sin (sweepHalf),
                               scaleArea.getHeight() - 12.0f);
        d.pivot  = { scaleArea.getCentreX(), scaleArea.getCentreY() + d.radius * 0.5f };
        d.valid  = d.radius >= 8.0f;

        return d;
    }

    /** Paper grain, in blocks one logical pixel across so the texture reads the
        same at every display scale instead of getting finer as you zoom in. */
    void applyGrain (juce::Image& image, int block, int seed)
    {
        const juce::Image::BitmapData data (image, juce::Image::BitmapData::readWrite);
        juce::Random rng (seed);

        for (int by = 0; by < data.height; by += block)
        {
            for (int bx = 0; bx < data.width; bx += block)
            {
                // Enough to break up the gradient, not enough to read as dirt.
                const float n     = rng.nextFloat() - 0.5f;
                const float lift  = 1.0f + n * 0.055f;
                const bool  speck = rng.nextFloat() > 0.9985f;
                const float mul   = speck ? 0.90f : lift;

                const int yEnd = juce::jmin (by + block, data.height);
                const int xEnd = juce::jmin (bx + block, data.width);

                for (int y = by; y < yEnd; ++y)
                {
                    for (int x = bx; x < xEnd; ++x)
                    {
                        const auto c = data.getPixelColour (x, y);

                        if (c.getAlpha() != 0)
                            data.setPixelColour (x, y, c.withMultipliedBrightness (mul));
                    }
                }
            }
        }
    }
}

NeedleMeter::NeedleMeter (std::function<double()> readIn, Mode modeIn, Range rangeIn,
                          juce::String captionIn)
    : read (std::move (readIn)),
      mode (modeIn),
      range (rangeIn),
      caption (captionIn.toUpperCase()),
      displayDb (rangeIn.restDb),
      lastPaintedDb (rangeIn.restDb)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

NeedleMeter::~NeedleMeter() = default;

double NeedleMeter::progressFor (double db, Range range) noexcept
{
    const double span = range.fullDb - range.restDb;

    return std::abs (span) < 1.0e-9 ? 0.0 : (db - range.restDb) / span;
}

double NeedleMeter::advance (double displayDb, double targetDb, Range range, double dtMs) noexcept
{
    // Climbing the scale is attack, falling back toward rest is release.
    // Comparing signed progress rather than |value - rest| keeps that true for
    // a reading that has overshot past the resting end — with the absolute
    // form, a needle sitting below rest treats the next rise as a release and
    // comes up six times too slowly.
    const bool attacking = progressFor (targetDb, range) > progressFor (displayDb, range);

    const double tau   = attacking ? attackMs : releaseMs;
    const double alpha = 1.0 - std::exp (-juce::jmax (0.0, dtMs) / tau);

    return displayDb + (targetDb - displayDb) * alpha;
}

void NeedleMeter::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    // A stalled message thread must not teleport the needle: clamped to 100 ms,
    // a long stall costs a few extra frames of catching up instead.
    const double dt  = lastTickMs > 0.0 ? juce::jlimit (0.0, 100.0, now - lastTickMs)
                                        : 1000.0 / 30.0;
    lastTickMs = now;

    displayDb = advance (displayDb, read ? read() : range.restDb, range, dt);

    if (std::abs (displayDb - lastPaintedDb) > 1.0e-3)
    {
        lastPaintedDb = displayDb;
        repaint();
    }
}

float NeedleMeter::angleFor (double db) const
{
    const float t = (float) juce::jlimit (0.0, 1.0, progressFor (db, range));

    const float restAngle = (mode == Mode::GR) ?  sweepHalf : -sweepHalf;
    const float fullAngle = (mode == Mode::GR) ? -sweepHalf :  sweepHalf;

    return restAngle + t * (fullAngle - restAngle);
}

void NeedleMeter::rebuildFace (juce::Rectangle<float> bounds, float scale)
{
    const int pw = juce::jmax (1, juce::roundToInt (bounds.getWidth()  * scale));
    const int ph = juce::jmax (1, juce::roundToInt (bounds.getHeight() * scale));

    faceCache = juce::Image (juce::Image::ARGB, pw, ph, true);

    juce::Graphics g (faceCache);
    g.addTransform (juce::AffineTransform::scale (scale));

    // Work in logical coordinates from here, with the face at the origin.
    const auto b = bounds.withZeroOrigin();
    const auto dial = dialFor (b, caption.isNotEmpty());

    const auto faceShape = theme::squircle (b, corner);

    // Unlit glass — the base the lamp sits on.
    g.setColour (theme::faceEdge);
    g.fillPath (faceShape);

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (faceShape);

        // The bulb: hottest just under the scale arc, falling away to the
        // corners, so the face reads as lit from within.
        const auto  centre     = juce::Point<float> (dial.pivot.x, dial.pivot.y - dial.radius * 0.42f);
        const float lampRadius = dial.radius * 1.85f;

        // The falloff never reaches zero — the whole card stays amber, with the
        // gradient shaping where it is hottest rather than where it survives.
        juce::ColourGradient lamp (theme::woodLight.withAlpha (0.95f), centre.x, centre.y,
                                   theme::amber.withAlpha (0.46f),    centre.x, centre.y + lampRadius,
                                   true);
        lamp.addColour (0.34, theme::amber.withAlpha (0.90f));
        lamp.addColour (0.70, theme::amber.withAlpha (0.66f));

        g.setGradientFill (lamp);
        g.fillPath (faceShape);

        // Uneven throw: a bulb behind card never lights it evenly.  Fixed seed,
        // so the mottling is a property of the meter rather than of the frame.
        {
            juce::Random rng (0x5eed4a11);

            for (int i = 0; i < 16; ++i)
            {
                const float r  = dial.radius * (0.22f + rng.nextFloat() * 0.55f);
                const float cx = b.getX() + b.getWidth()  * rng.nextFloat();
                const float cy = b.getY() + b.getHeight() * rng.nextFloat();
                const auto  tint = rng.nextBool() ? theme::woodLight : theme::woodShadow;

                juce::ColourGradient blob (tint.withAlpha (0.06f), cx, cy,
                                           tint.withAlpha (0.0f),  cx, cy + r, true);
                g.setGradientFill (blob);
                g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre ({ cx, cy }));
            }
        }

        // Vignette — just enough to seat the card in its housing.  Any heavier
        // and it eats the amber it is supposed to be framing.
        {
            const float reach = juce::jmax (b.getWidth(), b.getHeight()) * 0.85f;
            juce::ColourGradient vignette (theme::woodShadow.withAlpha (0.0f),
                                           b.getCentreX(), b.getCentreY(),
                                           theme::woodShadow.withAlpha (0.26f),
                                           b.getCentreX(), b.getCentreY() + reach, true);
            vignette.addColour (0.70, theme::woodShadow.withAlpha (0.03f));
            g.setGradientFill (vignette);
            g.fillPath (faceShape);
        }
    }

    // ---- scale ----------------------------------------------------------
    {
        juce::Path arc;
        arc.addCentredArc (dial.pivot.x, dial.pivot.y, dial.radius * 0.90f, dial.radius * 0.90f,
                           0.0f, -sweepHalf, sweepHalf, true);
        g.setColour (theme::woodShadow.withAlpha (0.8f));
        g.strokePath (arc, juce::PathStrokeType (1.2f));
    }

    {
        const double lo   = juce::jmin (range.restDb, range.fullDb);
        const double hi   = juce::jmax (range.restDb, range.fullDb);
        const double step = tickStepFor (hi - lo);

        g.setFont (theme::mono (9.0f));

        for (double v = lo; v <= hi + step * 0.01; v += step)
        {
            const float angle = angleFor (v);
            const float sinA  = std::sin (angle);
            const float cosA  = std::cos (angle);

            // Angle 0 points straight up, positive rotates clockwise.
            auto pointAt = [&] (float r)
            {
                return juce::Point<float> (dial.pivot.x + r * sinA, dial.pivot.y - r * cosA);
            };

            g.setColour (theme::woodShadow);
            g.drawLine ({ pointAt (dial.radius * 0.80f), pointAt (dial.radius * 0.90f) }, 1.4f);

            const int shown = (int) std::round (mode == Mode::GR ? std::abs (v) : v);

            g.setColour (theme::faceEdge);
            g.drawText (juce::String (shown),
                        juce::Rectangle<float> (26.0f, 11.0f).withCentre (pointAt (dial.radius * 0.66f)),
                        juce::Justification::centred, false);
        }
    }

    if (caption.isNotEmpty())
    {
        g.setColour (theme::woodShadow);
        g.setFont (theme::monoTracked (9.0f, true));
        g.drawText (caption, dial.captionRow, juce::Justification::centred, false);
    }

    // Grain goes on before the glass, so the texture sits under the reflection
    // the way printed card sits under a cover.
    applyGrain (faceCache, juce::jmax (1, juce::roundToInt (scale)), 0x9e3779b9);

    // ---- glass ----------------------------------------------------------
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (faceShape);

        // A slanted reflection band, not a flat top-down wash.
        juce::Path sheen;
        sheen.startNewSubPath (b.getX() - b.getWidth() * 0.1f, b.getY() + b.getHeight() * 0.62f);
        sheen.lineTo (b.getX() + b.getWidth() * 0.62f, b.getY() - b.getHeight() * 0.1f);
        sheen.lineTo (b.getX() + b.getWidth() * 1.10f, b.getY() - b.getHeight() * 0.1f);
        sheen.lineTo (b.getX() - b.getWidth() * 0.1f,  b.getY() + b.getHeight() * 1.05f);
        sheen.closeSubPath();

        // The gradient runs perpendicular to the band and reaches zero exactly
        // on its lower edge, so the reflection fades out instead of ending on a
        // visible diagonal line.
        juce::ColourGradient gloss (theme::labelCream.withAlpha (0.16f),
                                    b.getX() - b.getWidth() * 0.05f,
                                    b.getY() - b.getHeight() * 0.05f,
                                    theme::labelCream.withAlpha (0.0f),
                                    b.getX() + b.getWidth() * 0.26f,
                                    b.getY() + b.getHeight() * 0.26f, false);
        g.setGradientFill (gloss);
        g.fillPath (sheen);
    }

}

void NeedleMeter::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    const auto dial = dialFor (b, caption.isNotEmpty());

    // Too small to seat a movement in.  Bailing out *before* the cache matters:
    // caching a face built at an unusable size would leave a blank image keyed
    // to bounds the component may well be given again.
    if (! dial.valid)
        return;

    const float scale = juce::jlimit (1.0f, 4.0f,
                                      g.getInternalContext().getPhysicalPixelScaleFactor());

    if (faceCache.isNull() || cachedBounds != getLocalBounds() || cachedScale != scale)
    {
        rebuildFace (b, scale);
        cachedBounds = getLocalBounds();
        cachedScale  = scale;
    }

    g.drawImage (faceCache, b);

    // The needle is the only live part — a black pointer over the lit card,
    // with a soft shadow so it sits above the dial rather than printed into it.
    const float angle = angleFor (displayDb);

    juce::Path needle;
    needle.startNewSubPath (0.0f, -dial.radius * 0.88f);
    needle.lineTo (0.0f, dial.radius * 0.06f);

    const auto rotate = juce::AffineTransform::rotation (angle).translated (dial.pivot);

    g.setColour (theme::woodShadow.withAlpha (0.35f));
    g.strokePath (needle, juce::PathStrokeType (3.6f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded),
                  rotate.translated (1.0f, 1.5f));

    g.setColour (theme::bg);
    g.strokePath (needle, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded),
                  rotate);

    const float hub = juce::jmax (3.0f, dial.radius * 0.06f);
    g.setColour (theme::bg);
    g.fillEllipse (juce::Rectangle<float> (hub * 2.0f, hub * 2.0f).withCentre (dial.pivot));
    g.setColour (theme::woodLight.withAlpha (0.5f));
    g.drawEllipse (juce::Rectangle<float> (hub * 2.0f, hub * 2.0f).withCentre (dial.pivot), 1.0f);
}

} // namespace ferment

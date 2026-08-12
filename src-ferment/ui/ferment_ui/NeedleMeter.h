#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "FermentTheme.h"

#include <functional>

namespace ferment
{

/** An analog needle meter — gain reduction or level.

    The ballistics live in here, not in the processor: the component is handed
    an instantaneous dB reading and does its own 50 ms attack / 300 ms release
    exponential smoothing against real elapsed time, so a meter accessor that
    jumps in block-sized steps still produces a needle that swings.

    It owns its own Timer and only repaints when the needle has actually moved.
*/
class NeedleMeter : public juce::Component,
                    private juce::Timer
{
public:
    /** GR rests at the right of the scale and deflects left; Level rests at
        the left. */
    enum class Mode { GR, Level };

    /** The dB value at the needle's resting end of the scale and at the far,
        fully deflected end.  A 0..-12 dB GR meter is { 0.0, -12.0 }. */
    struct Range
    {
        double restDb;
        double fullDb;
    };

    /** @param read   returns the instantaneous dB reading, polled at 30 Hz
        @param mode   which end of the scale the needle rests at
        @param range  the scale's rest and full-deflection values
        @param caption optional label under the scale, e.g. "GR" */
    NeedleMeter (std::function<double()> read, Mode mode, Range range,
                 juce::String caption = {});
    ~NeedleMeter() override;

    void paint (juce::Graphics&) override;

    /** Ballistics, in milliseconds. */
    static constexpr double attackMs  = 50.0;
    static constexpr double releaseMs = 300.0;

    /** How far along the scale a reading sits: 0 at the resting end, 1 at full
        deflection, signed outside that.  Works whichever way round the range
        runs, so a 0..-12 GR scale and a -60..0 level scale share the maths. */
    static double progressFor (double db, Range range) noexcept;

    /** One ballistics step, in isolation: moves @a displayDb toward @a targetDb
        over @a dtMs, attacking when the reading climbs the scale and releasing
        when it falls back.  Pure and static so the ballistics are testable
        without a message loop. */
    static double advance (double displayDb, double targetDb, Range range, double dtMs) noexcept;

private:
    void timerCallback() override;
    float angleFor (double db) const;

    /*  Everything that does not move — lamp, grain, scale, glass — is rendered
        once into an image and blitted; only the needle is drawn live.  The
        grain is per-pixel, so regenerating it on every needle step would be
        pointless work at 30 Hz.  The cache is keyed on physical size, so it is
        rebuilt when the host rescales the editor and stays sharp at 2x.
    */
    void rebuildFace (juce::Rectangle<float> bounds, float scale);

    juce::Image faceCache;
    juce::Rectangle<int> cachedBounds;
    float cachedScale = 0.0f;

    std::function<double()> read;
    Mode mode;
    Range range;
    juce::String caption;

    double displayDb     = 0.0;
    double lastPaintedDb = 0.0;
    double lastTickMs    = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeedleMeter)
};

} // namespace ferment

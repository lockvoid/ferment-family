#include "LearnButton.h"

#include <cmath>

namespace ferment
{

LearnButton::LearnButton()
    : juce::Button ("LEARN")
{
    setButtonText ("LEARN");
}

LearnButton::~LearnButton() = default;

void LearnButton::setState (State newState, double newProgress)
{
    const auto clamped = juce::jlimit (0.0, 1.0, newProgress);

    if (state == newState && std::abs (clamped - progress) < 0.005)
        return;

    const bool entering = state != newState;
    state = newState;
    progress = clamped;

    if (entering && state == State::Done)
    {
        // Real elapsed time, not a tick count: the flash has to last the same
        //900 ms whether or not the host is starving the message thread.
        flashStartMs = juce::Time::getMillisecondCounterHiRes();
        flash = 1.0f;
        startTimerHz (30);
    }

    setButtonText (captionFor (state));
    repaint();
}

void LearnButton::timerCallback()
{
    const double elapsed = juce::Time::getMillisecondCounterHiRes() - flashStartMs;

    if (elapsed >= flashMs)
    {
        stopTimer();
        flash = 0.0f;
        state = State::Idle;
        setButtonText (captionFor (state));
    }
    else
    {
        flash = 1.0f - (float) (elapsed / flashMs);
    }

    repaint();
}

juce::String LearnButton::captionFor (State s) const
{
    switch (s)
    {
        case State::Listening: return "LISTENING";
        case State::Setting:   return "SETTING LEVELS";
        case State::Done:      return "LEARNED";
        case State::Idle:
        default:               return "LEARN";
    }
}

void LearnButton::paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const float radius = cornerRadius;

    const bool busy = state == State::Listening || state == State::Setting;

    // Idle is a dark pill with an amber rim; a run lights it, and the flash at
    // the end fades that light back down rather than snapping it off.
    float lit = state == State::Done ? flash : 0.0f;

    if (shouldDrawButtonAsDown)
        lit = juce::jmax (lit, 0.35f);
    else if (shouldDrawButtonAsHighlighted && ! busy)
        lit = juce::jmax (lit, 0.12f);

    g.setColour (theme::faceEdge.interpolatedWith (theme::amber, lit));
    g.fillRoundedRectangle (bounds, radius);

    g.setColour (theme::amber.withAlpha (busy ? 0.55f : 0.9f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.5f);

    if (state == State::Listening)
    {
        /*  Phase one has a known length, so it gets a real progress bar rather
            than a spinner: "how much longer" is the only question anyone has
            while holding a track at its loudest point. */
        auto fill = bounds.reduced (2.0f);
        fill = fill.withWidth (fill.getWidth() * (float) progress);

        if (fill.getWidth() > 2.0f)
        {
            juce::Graphics::ScopedSaveState saved (g);
            g.reduceClipRegion (fill.getSmallestIntegerContainer());
            g.setColour (theme::amber.withAlpha (0.35f));
            g.fillRoundedRectangle (bounds.reduced (2.0f), radius);
        }
    }
    else if (state == State::Setting)
    {
        // Phase two is core-timed and short; a full-width dim wash says "still
        // working" without promising a duration the wrapper does not know.
        g.setColour (theme::amber.withAlpha (0.18f));
        g.fillRoundedRectangle (bounds.reduced (2.0f), radius);
    }

    g.setColour (lit > 0.5f ? theme::faceEdge : theme::amber);
    g.setFont (theme::monoTracked (12.0f, true));
    g.drawFittedText (getButtonText(), getLocalBounds(), juce::Justification::centred, 1, 0.8f);
}

} // namespace ferment

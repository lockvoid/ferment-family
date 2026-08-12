#include "EqGraphEditor.h"

#include <algorithm>
#include <cmath>

namespace ferment
{

namespace
{
    constexpr float nodeRadius     = 5.5f;
    constexpr float nodeHitRadius  = 12.0f;
    constexpr float spectrumFloor  = -90.0f;   // dBFS at the bottom of the well

    /** Inverts one of the processor's normalised→real maps by bisection.

        Doing it numerically rather than hand-deriving each inverse means the
        UI cannot drift out of step with the DSP when a range is retuned —
        there is only ever one copy of the forward maths.

        The result is written to a float parameter, so 24 halvings of [0, 1]
        already land inside one ULP; more iterations only refine digits the
        parameter cannot store.  A constant or non-monotonic map does not
        diverge — bisection just walks to whichever end it was told to.
    */
    double invertMonotonic (const std::function<double (double)>& fn, double target)
    {
        if (! fn)
            return juce::jlimit (0.0, 1.0, target);

        const bool ascending = fn (1.0) >= fn (0.0);
        double lo = 0.0, hi = 1.0;

        for (int i = 0; i < 24; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            const bool below = ascending ? (fn (mid) < target) : (fn (mid) > target);

            if (below) lo = mid;
            else       hi = mid;
        }

        return 0.5 * (lo + hi);
    }

    juce::String formatHz (double hz)
    {
        if (hz >= 1000.0)
            return juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz";

        return juce::String ((int) std::round (hz)) + " Hz";
    }
}

EqGraphEditor::EqGraphEditor (juce::AudioProcessorValueTreeState& apvtsIn, Spec specIn)
    : apvts (apvtsIn), spec (std::move (specIn))
{
    spec.numBands = juce::jmax (0, spec.numBands);

    bands.reserve ((size_t) spec.numBands);

    for (int b = 0; b < spec.numBands; ++b)
    {
        BandParams bp;

        if (spec.paramId)
        {
            // An ID that does not resolve leaves a null here rather than a
            // dangling name; every use site checks, so a partial Spec draws a
            // flat band instead of crashing.
            bp.type = apvts.getParameter (spec.paramId (b, "type"));
            bp.freq = apvts.getParameter (spec.paramId (b, "freq"));
            bp.gain = apvts.getParameter (spec.paramId (b, "gain"));
            bp.q    = apvts.getParameter (spec.paramId (b, "q"));
            bp.on   = apvts.getParameter (spec.paramId (b, "on"));
        }

        bands.push_back (bp);
    }

    curveBiquads.resize ((size_t) spec.numBands);
    curveEnabled.resize ((size_t) spec.numBands, 0);

    ring.assign       ((size_t) fftSize, 0.0f);
    pullBuffer.assign ((size_t) fftSize, 0.0f);
    fftData.assign    ((size_t) fftSize * 2, 0.0f);
    binDb.assign      ((size_t) fftSize / 2, spectrumFloor);

    setWantsKeyboardFocus (false);
    startTimerHz (30);
}

EqGraphEditor::~EqGraphEditor() = default;

// ---------------------------------------------------------------------------
// geometry

juce::Rectangle<float> EqGraphEditor::graphArea() const
{
    return getLocalBounds().toFloat().reduced (1.0f);
}

float EqGraphEditor::xForFreq (double hz) const
{
    const auto r = graphArea();
    const double t = std::log (juce::jlimit (minHz, maxHz, hz) / minHz) / std::log (maxHz / minHz);
    return r.getX() + r.getWidth() * (float) t;
}

double EqGraphEditor::freqForX (float x) const
{
    const auto r = graphArea();
    const double t = r.getWidth() > 0.0f
                   ? juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth()))
                   : 0.0;
    return minHz * std::pow (maxHz / minHz, t);
}

float EqGraphEditor::yForDb (double db) const
{
    const auto r = graphArea();
    const double t = (maxDb - juce::jlimit (-maxDb, maxDb, db)) / (2.0 * maxDb);
    return r.getY() + r.getHeight() * (float) t;
}

double EqGraphEditor::dbForY (float y) const
{
    const auto r = graphArea();
    const double t = r.getHeight() > 0.0f
                   ? juce::jlimit (0.0, 1.0, (double) ((y - r.getY()) / r.getHeight()))
                   : 0.5;
    return maxDb - t * 2.0 * maxDb;
}

juce::Point<float> EqGraphEditor::nodeCentre (const BandState& s) const
{
    return { xForFreq (s.freq), yForDb (typeHasGain (s.type) ? s.gain : 0.0) };
}

juce::Point<float> EqGraphEditor::nodeCentre (int band) const
{
    return nodeCentre (bandState (band));
}

int EqGraphEditor::bandAt (juce::Point<float> p) const
{
    int best = -1;
    float bestDistance = nodeHitRadius;

    for (int b = 0; b < spec.numBands; ++b)
    {
        const float d = nodeCentre (b).getDistanceFrom (p);

        if (d < bestDistance)
        {
            bestDistance = d;
            best = b;
        }
    }

    return best;
}

// ---------------------------------------------------------------------------
// parameters

double EqGraphEditor::rawValue (const juce::RangedAudioParameter* p)
{
    return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0;
}

bool EqGraphEditor::typeHasGain (int type) const
{
    return type == spec.bell || type == spec.lowShelf || type == spec.highShelf;
}

double EqGraphEditor::sampleRate() const
{
    const double sr = spec.getSampleRate ? spec.getSampleRate() : 0.0;
    return sr > 0.0 ? sr : 48000.0;
}

EqGraphEditor::BandState EqGraphEditor::bandState (int band) const
{
    BandState s;

    if (band < 0 || band >= (int) bands.size())
        return s;

    const auto& bp = bands[(size_t) band];

    s.type  = (int) std::round (rawValue (bp.type));
    s.on    = rawValue (bp.on) >= 0.5;
    s.qNorm = rawValue (bp.q);

    s.freq = spec.freqFromNorm ? spec.freqFromNorm (rawValue (bp.freq)) : 1000.0;
    s.gain = spec.gainFromNorm ? spec.gainFromNorm (rawValue (bp.gain)) : 0.0;
    s.q    = spec.qFromNorm    ? spec.qFromNorm    (s.qNorm)            : 1.0;

    return s;
}

void EqGraphEditor::beginGesture (int band)
{
    if (band < 0 || band >= (int) bands.size())
        return;

    for (auto* p : { bands[(size_t) band].freq, bands[(size_t) band].gain, bands[(size_t) band].q })
        if (p != nullptr)
            p->beginChangeGesture();
}

void EqGraphEditor::endGesture (int band)
{
    if (band < 0 || band >= (int) bands.size())
        return;

    for (auto* p : { bands[(size_t) band].freq, bands[(size_t) band].gain, bands[(size_t) band].q })
        if (p != nullptr)
            p->endChangeGesture();
}

void EqGraphEditor::setNorm (juce::RangedAudioParameter* p, double norm)
{
    if (p != nullptr)
        p->setValueNotifyingHost ((float) juce::jlimit (0.0, 1.0, norm));
}

void EqGraphEditor::setType (int band, int type)
{
    if (band < 0 || band >= (int) bands.size() || spec.numTypes < 2)
        return;

    if (auto* p = bands[(size_t) band].type)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost ((float) juce::jlimit (0.0, 1.0,
            (double) type / (double) (spec.numTypes - 1)));
        p->endChangeGesture();
    }

    repaint();
}

void EqGraphEditor::toggleBand (int band)
{
    if (band < 0 || band >= (int) bands.size())
        return;

    if (auto* p = bands[(size_t) band].on)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() >= 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }

    repaint();
}

void EqGraphEditor::resetBand (int band)
{
    if (band < 0 || band >= (int) bands.size())
        return;

    const auto& bp = bands[(size_t) band];

    for (auto* p : { bp.type, bp.freq, bp.gain, bp.q, bp.on })
    {
        if (p != nullptr)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
    }

    repaint();
}

void EqGraphEditor::showBandMenu (int band)
{
    if (band < 0 || band >= (int) bands.size())
        return;

    const auto state = bandState (band);

    juce::PopupMenu types;

    for (int t = 0; t < spec.numTypes; ++t)
        types.addItem (t + 1,
                       spec.typeName ? spec.typeName (t) : juce::String (t),
                       true,
                       t == state.type);

    juce::PopupMenu menu;
    menu.addSectionHeader ("Band " + juce::String (band + 1));
    menu.addSubMenu ("Type", types);
    menu.addSeparator();
    menu.addItem (1001, state.on ? "Disable band" : "Enable band");
    menu.addItem (1002, "Reset band");

    juce::Component::SafePointer<EqGraphEditor> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe, band] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            if (result >= 1 && result <= safe->spec.numTypes)
                                safe->setType (band, result - 1);
                            else if (result == 1001)
                                safe->toggleBand (band);
                            else if (result == 1002)
                                safe->resetBand (band);
                        });
}

// ---------------------------------------------------------------------------
// interaction

void EqGraphEditor::setSelectedBand (int band)
{
    band = juce::jlimit (0, juce::jmax (0, spec.numBands - 1), band);

    if (band == selectedBand)
        return;

    selectedBand = band;
    repaint();

    if (onBandSelected)
        onBandSelected (band);
}

void EqGraphEditor::mouseDown (const juce::MouseEvent& e)
{
    const int band = bandAt (e.position);

    if (e.mods.isPopupMenu())
    {
        if (band >= 0)
        {
            setSelectedBand (band);
            showBandMenu (band);
        }

        return;
    }

    if (band < 0)
        return;

    setSelectedBand (band);

    draggingBand = band;
    dragStartQNorm = bandState (band).qNorm;
    beginGesture (band);
}

void EqGraphEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingBand < 0)
        return;

    const auto& bp = bands[(size_t) draggingBand];

    if (e.mods.isCommandDown())
    {
        // Cmd + vertical drag rides Q; 200 px covers the whole range.
        const double delta = -(double) e.getDistanceFromDragStartY() / 200.0;
        setNorm (bp.q, dragStartQNorm + delta);
        repaint();
        return;
    }

    setNorm (bp.freq, invertMonotonic (spec.freqFromNorm, freqForX (e.position.x)));

    if (typeHasGain (bandState (draggingBand).type))
        setNorm (bp.gain, invertMonotonic (spec.gainFromNorm, dbForY (e.position.y)));

    repaint();
}

void EqGraphEditor::mouseUp (const juce::MouseEvent&)
{
    if (draggingBand < 0)
        return;

    endGesture (draggingBand);
    draggingBand = -1;
    repaint();
}

void EqGraphEditor::mouseMove (const juce::MouseEvent& e)
{
    const int band = bandAt (e.position);

    if (band != hoveredBand)
    {
        hoveredBand = band;
        repaint();
    }
}

void EqGraphEditor::mouseExit (const juce::MouseEvent&)
{
    if (hoveredBand != -1)
    {
        hoveredBand = -1;
        repaint();
    }
}

void EqGraphEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int band = bandAt (e.position);

    if (band >= 0)
        toggleBand (band);
}

void EqGraphEditor::mouseWheelMove (const juce::MouseEvent& e,
                                    const juce::MouseWheelDetails& wheel)
{
    const int band = bandAt (e.position);

    if (band < 0)
        return;

    auto* p = bands[(size_t) band].q;

    if (p == nullptr)
        return;

    const float target = juce::jlimit (0.0f, 1.0f, p->getValue() + wheel.deltaY * 0.25f);

    // A drag already holds a gesture open on this band's Q — a trackpad scroll
    // mid-drag is easy to do by accident.  Opening a second one inside it is an
    // unbalanced gesture to the host (and a jassert in a debug JUCE), so ride
    // the drag's gesture instead of starting another.
    if (band == draggingBand)
    {
        p->setValueNotifyingHost (target);
    }
    else
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (target);
        p->endChangeGesture();
    }

    repaint();
}

// ---------------------------------------------------------------------------
// painting

void EqGraphEditor::paint (juce::Graphics& g)
{
    const auto r = graphArea();

    if (r.getWidth() < 8.0f || r.getHeight() < 8.0f)
        return;

    const float corner = 6.0f;

    // A recessed well, so the grid and the curve have something to sit on.
    g.setColour (theme::bg);
    g.fillRoundedRectangle (r, corner);

    {
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path well;
        well.addRoundedRectangle (r, corner);
        g.reduceClipRegion (well);

        paintSpectrum (g, r);
        paintGrid     (g, r);
        paintCurve    (g, r);
        paintNodes    (g);
    }

    paintReadout (g, r);

    g.setColour (theme::faceEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.5f);
}

void EqGraphEditor::paintGrid (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (theme::faceEdge);

    static const double freqTicks[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };

    for (double f : freqTicks)
    {
        const float x = xForFreq (f);
        g.drawLine (x, r.getY(), x, r.getBottom(), 0.8f);
    }

    for (int db = -18; db <= 18; db += 6)
    {
        const float y = yForDb (db);
        g.drawLine (r.getX(), y, r.getRight(), y, db == 0 ? 1.5f : 0.8f);
    }

    // Axis numerals: not in the token list's remit, so they use labelDim.
    g.setColour (theme::labelDim);
    g.setFont (theme::mono (9.0f));

    for (double f : { 100.0, 1000.0, 10000.0 })
        g.drawText (formatHz (f),
                    juce::Rectangle<float> (46.0f, 11.0f)
                        .withCentre ({ xForFreq (f), r.getBottom() - 8.0f }),
                    juce::Justification::centred, false);

    for (int db : { -12, 12 })
        g.drawText ((db > 0 ? "+" : "") + juce::String (db),
                    juce::Rectangle<float> (28.0f, 11.0f)
                        .withCentre ({ r.getX() + 18.0f, yForDb (db) }),
                    juce::Justification::centred, false);
}

void EqGraphEditor::paintSpectrum (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (! haveSpectrum)
        return;

    const double sr   = sampleRate();
    const int    bins = fftSize / 2;

    juce::Path p;
    p.startNewSubPath (r.getX(), r.getBottom());

    bool any = false;

    for (int i = 1; i < bins; ++i)
    {
        const double f = (double) i * sr / (double) fftSize;

        if (f < minHz)
            continue;

        if (f > maxHz)
            break;

        const float t = juce::jlimit (0.0f, 1.0f,
                                      (binDb[(size_t) i] - spectrumFloor) / -spectrumFloor);
        p.lineTo (xForFreq (f), r.getBottom() - r.getHeight() * t);
        any = true;
    }

    if (! any)
        return;

    p.lineTo (r.getRight(), r.getBottom());
    p.closeSubPath();

    juce::ColourGradient grad (theme::amber.withAlpha (0.20f),    r.getCentreX(), r.getY(),
                               theme::faceEdge.withAlpha (0.20f), r.getCentreX(), r.getBottom(),
                               false);
    g.setGradientFill (grad);
    g.fillPath (p);
}

void EqGraphEditor::updateResponse()
{
    const double sr = curveSampleRate = sampleRate();
    const double nyquist = sr * 0.49;

    for (int i = 0; i < spec.numBands; ++i)
    {
        const auto s = bandState (i);
        curveEnabled[(size_t) i] = s.on ? 1 : 0;

        auto& bq = curveBiquads[(size_t) i];
        const double f = juce::jlimit (minHz, nyquist, s.freq);

        if      (s.type == spec.bell)      bq.setBell      (f, s.q, s.gain, sr);
        else if (s.type == spec.lowShelf)  bq.setLowShelf  (f, s.q, s.gain, sr);
        else if (s.type == spec.highShelf) bq.setHighShelf (f, s.q, s.gain, sr);
        else if (s.type == spec.lowCut)    bq.setLowCut    (f, s.q, sr);
        else if (s.type == spec.highCut)   bq.setHighCut   (f, s.q, sr);
        else if (s.type == spec.notch)     bq.setNotch     (f, s.q, sr);
        else                               bq.setIdentity();
    }
}

double EqGraphEditor::responseDbAt (double hz) const
{
    double totalDb = 0.0;

    for (int i = 0; i < spec.numBands; ++i)
        if (curveEnabled[(size_t) i] != 0)
            totalDb += curveBiquads[(size_t) i].responseDb (hz, curveSampleRate);

    return totalDb;
}

void EqGraphEditor::paintCurve (juce::Graphics& g, juce::Rectangle<float> r)
{
    updateResponse();

    const int numPoints = juce::jmax (2, (int) r.getWidth());

    juce::Path curve;

    for (int px = 0; px < numPoints; ++px)
    {
        const double t = (double) px / (double) (numPoints - 1);
        const double freq = minHz * std::pow (maxHz / minHz, t);

        const float x = r.getX() + (float) px;
        const float y = yForDb (responseDbAt (freq));

        if (px == 0) curve.startNewSubPath (x, y);
        else         curve.lineTo (x, y);
    }

    // Soft glow down to the zero line.
    {
        juce::Path fill = curve;
        fill.lineTo (r.getRight(), yForDb (0.0));
        fill.lineTo (r.getX(),     yForDb (0.0));
        fill.closeSubPath();

        g.setColour (theme::amber.withAlpha (0.12f));
        g.fillPath (fill);
    }

    g.setColour (theme::amber);
    g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
}

void EqGraphEditor::paintNodes (juce::Graphics& g)
{
    for (int b = 0; b < spec.numBands; ++b)
    {
        const auto s = bandState (b);
        const auto c = nodeCentre (s);

        const bool active = (b == draggingBand) || (b == hoveredBand) || (b == selectedBand);
        const float radius = nodeRadius * (active ? 1.45f : 1.0f);

        const auto body = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c);

        g.setColour (theme::bg.withAlpha (0.75f));
        g.fillEllipse (body.expanded (1.5f));

        if (s.on)
        {
            g.setColour (theme::amber.withAlpha (b == selectedBand ? 1.0f : 0.75f));
            g.fillEllipse (body);
            g.setColour (theme::faceEdge);
            g.drawEllipse (body, 1.0f);
        }
        else
        {
            g.setColour (theme::labelDim);
            g.drawEllipse (body, 1.5f);
        }

        g.setColour (s.on ? theme::faceEdge : theme::labelDim);
        g.setFont (theme::mono (8.5f, true));
        g.drawText (juce::String (b + 1), body, juce::Justification::centred, false);
    }
}

void EqGraphEditor::paintReadout (juce::Graphics& g, juce::Rectangle<float> r)
{
    const int band = draggingBand >= 0 ? draggingBand : hoveredBand;

    if (band < 0)
        return;

    const auto s = bandState (band);

    juce::String text = formatHz (s.freq);

    if (typeHasGain (s.type))
        text += (s.gain >= 0.0 ? "  +" : "  ") + juce::String (s.gain, 1) + " dB";

    text += "  Q " + juce::String (s.q, 2);

    g.setFont (theme::mono (10.0f));
    const float width  = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 16.0f;
    const float height = 18.0f;

    auto chip = juce::Rectangle<float> (width, height)
                    .withCentre (nodeCentre (band).translated (0.0f, -22.0f))
                    .constrainedWithin (r.reduced (4.0f));

    g.setColour (theme::faceTop.withAlpha (0.92f));
    g.fillRoundedRectangle (chip, 3.0f);
    g.setColour (theme::woodDark);
    g.drawRoundedRectangle (chip, 3.0f, 1.0f);

    g.setColour (theme::amber);
    g.drawText (text, chip, juce::Justification::centred, false);
}

// ---------------------------------------------------------------------------
// spectrum

void EqGraphEditor::timerCallback()
{
    pumpSpectrum();
    repaint();
}

void EqGraphEditor::pumpSpectrum()
{
    if (! spec.readSpectrumSamples)
        return;

    for (;;)
    {
        const int got = spec.readSpectrumSamples (pullBuffer.data(), (int) pullBuffer.size());

        if (got <= 0)
            break;

        for (int i = 0; i < got; ++i)
        {
            ring[(size_t) ringWrite] = pullBuffer[(size_t) i];
            ringWrite = (ringWrite + 1) % fftSize;
        }

        haveSpectrum = true;

        if (got < (int) pullBuffer.size())
            break;
    }

    if (! haveSpectrum)
        return;

    // A rolling window over the newest fftSize samples, so the analyser still
    // refreshes at the full 30 Hz even though a frame is only ~1600 samples.
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = ring[(size_t) ((ringWrite + i) % fftSize)];

    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);

    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const float norm = 2.0f / (float) fftSize;

    for (int i = 0; i < fftSize / 2; ++i)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[(size_t) i] * norm, spectrumFloor);
        binDb[(size_t) i] = juce::jmax (db, binDb[(size_t) i] - 1.5f);   // instant rise, slow fall
    }
}

} // namespace ferment

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "FermentTheme.h"
#include "../../common/Biquad.h"

#include <functional>
#include <vector>

namespace ferment
{

/** The Ferment EQ response graph: log-frequency curve, draggable band nodes,
    spectrum underlay.

    The component knows nothing about FermentEqProcessor.  The host editor
    hands over the parameter-ID scheme and the range conversions the processor
    already defines, so the drawn curve can never drift from the DSP's own
    maths, and the graph stays a UI component rather than a second copy of the
    EQ's ABI.

    Every edit goes out through setValueNotifyingHost inside a
    begin/endChangeGesture pair, so drags record as automation.
*/
class EqGraphEditor : public juce::Component,
                      private juce::Timer
{
public:
    struct Spec
    {
        int numBands = 8;

        /** Builds an APVTS parameter ID — FermentEq's "b{n}_{suffix}". */
        std::function<juce::String (int band, const char* suffix)> paramId;

        /** The processor's own normalised→real conversions. */
        std::function<double (double)> freqFromNorm;
        std::function<double (double)> gainFromNorm;
        std::function<double (double)> qFromNorm;

        /** Host sample rate, so the drawn response matches what the DSP is
            actually running.  Falls back to 48 kHz when absent or not yet
            prepared. */
        std::function<double()> getSampleRate;

        /** Band types, in the processor's enum order. */
        int bell = 0, lowShelf = 1, highShelf = 2, lowCut = 3, highCut = 4, notch = 5;
        int numTypes = 6;
        std::function<juce::String (int type)> typeName;

        /** Drains up to maxSamples of post-EQ audio for the spectrum underlay
            and returns how many were read.  Optional — leave it unset and the
            graph simply draws without a spectrum. */
        std::function<int (float* dest, int maxSamples)> readSpectrumSamples;
    };

    EqGraphEditor (juce::AudioProcessorValueTreeState& apvts, Spec spec);
    ~EqGraphEditor() override;

    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    int  getSelectedBand() const noexcept { return selectedBand; }
    void setSelectedBand (int band);

    /** Re-reads every band's parameters into the response the curve is drawn
        from.  paint() calls this; anything asking responseDbAt() for the drawn
        value outside a paint must call it first. */
    void updateResponse();

    /** The dB value the curve is drawn at for a frequency, as of the last
        updateResponse().  This is the number the graph is claiming about the
        audio, so it is what a response test should be comparing. */
    double responseDbAt (double hz) const;

    /** Fires when the user picks a different band, so the editor's knob row
        can follow the selection. */
    std::function<void (int)> onBandSelected;

    static constexpr double minHz = 20.0;
    static constexpr double maxHz = 20000.0;
    static constexpr double maxDb = 18.0;      // dB axis is +/-18

private:
    /*  Resolved once in the constructor.  The graph reads every band on every
        repaint and again on every mouse move, and an APVTS lookup is a string
        hash — holding the parameters directly keeps that off the paint path.
        They outlive the editor: the APVTS owns them for the processor's life. */
    struct BandParams
    {
        juce::RangedAudioParameter* type = nullptr;
        juce::RangedAudioParameter* freq = nullptr;
        juce::RangedAudioParameter* gain = nullptr;
        juce::RangedAudioParameter* q    = nullptr;
        juce::RangedAudioParameter* on   = nullptr;
    };

    struct BandState
    {
        int    type   = 0;
        double freq   = 1000.0;
        double gain   = 0.0;
        double q      = 1.0;
        double qNorm  = 0.5;
        bool   on     = true;
    };

    // --- geometry ---------------------------------------------------------
    juce::Rectangle<float> graphArea() const;
    float  xForFreq (double hz) const;
    double freqForX (float x) const;
    float  yForDb   (double db) const;
    double dbForY   (float y) const;
    juce::Point<float> nodeCentre (const BandState&) const;
    juce::Point<float> nodeCentre (int band) const;
    int    bandAt (juce::Point<float>) const;

    // --- parameter access -------------------------------------------------
    static double rawValue (const juce::RangedAudioParameter* p);
    BandState bandState (int band) const;
    bool  typeHasGain (int type) const;
    double sampleRate() const;

    void beginGesture (int band);
    void endGesture (int band);
    static void setNorm (juce::RangedAudioParameter* p, double norm);
    void setType (int band, int type);
    void toggleBand (int band);
    void resetBand (int band);
    void showBandMenu (int band);

    // --- painting ---------------------------------------------------------
    void paintGrid     (juce::Graphics&, juce::Rectangle<float>);
    void paintSpectrum (juce::Graphics&, juce::Rectangle<float>);
    void paintCurve    (juce::Graphics&, juce::Rectangle<float>);
    void paintNodes    (juce::Graphics&);
    void paintReadout  (juce::Graphics&, juce::Rectangle<float>);

    void timerCallback() override;
    void pumpSpectrum();

    juce::AudioProcessorValueTreeState& apvts;
    Spec spec;
    std::vector<BandParams> bands;

    int selectedBand = 0;
    int hoveredBand  = -1;
    int draggingBand = -1;
    double dragStartQNorm = 0.5;

    // Response curve scratch, sized once so paint() does not allocate.  The
    // sample rate is snapshotted alongside it, so the curve is evaluated at the
    // rate its coefficients were built for and paint() is not asking the
    // processor for it once per pixel.
    std::vector<Biquad> curveBiquads;
    std::vector<char>   curveEnabled;
    double curveSampleRate = 48000.0;

    // --- spectrum ---------------------------------------------------------
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;   // 2048

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize,
                                                 juce::dsp::WindowingFunction<float>::hann,
                                                 false };
    std::vector<float> ring;        // last fftSize samples, circular
    std::vector<float> pullBuffer;
    std::vector<float> fftData;     // 2 * fftSize, as performFrequencyOnly... requires
    std::vector<float> binDb;
    int  ringWrite = 0;
    bool haveSpectrum = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqGraphEditor)
};

} // namespace ferment

// Ferment Utility DSP tests.
//
// Each block runs the processor with a crafted input + parameter set
// and asserts an observable property of the output.

#include "../src-ferment/utility/FermentUtilityProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace
{
    constexpr double kSR = 48000.0;
    constexpr int    kBlock = 512;

    using Proc = FermentUtilityProcessor;

    // Fill buffer with sine @ freq / amp on both L and R (in phase unless invertR).
    void fillSine(juce::AudioBuffer<float>& buf, double freq, double amp, bool invertR = false)
    {
        const int N = buf.getNumSamples();
        float* L = buf.getWritePointer(0);
        float* R = buf.getNumChannels() > 1 ? buf.getWritePointer(1) : nullptr;
        for (int n = 0; n < N; ++n)
        {
            const double x = std::sin(2.0 * M_PI * freq * n / kSR);
            L[n] = (float)(amp * x);
            if (R) R[n] = (float)((invertR ? -amp : amp) * x);
        }
    }

    double dbFromRms(double r) { return 20.0 * std::log10(std::max(r, 1e-12)); }

    double rms(const juce::AudioBuffer<float>& buf, int ch, int skip = 0)
    {
        const int N = buf.getNumSamples();
        const float* x = buf.getReadPointer(ch);
        double s = 0.0;
        int count = 0;
        for (int n = skip; n < N; ++n) { s += (double)x[n] * x[n]; ++count; }
        return std::sqrt(s / std::max(count, 1));
    }

    // Run the processor once on a prepared buffer.  Returns the modified buffer.
    void runOnce(Proc& p, juce::AudioBuffer<float>& buf)
    {
        juce::MidiBuffer mb;
        p.processBlock(buf, mb);
    }

    struct Check { const char* name; bool ok; double observed; double expected; const char* units; };
    int failures = 0;
    void print(const Check& c)
    {
        std::printf("  %-58s observed=%+8.3f %s  %s\n",
                    c.name, c.observed, c.units, c.ok ? "PASS" : "FAIL");
        if (!c.ok) failures++;
    }
    Check nearly(const char* n, double obs, double exp, double tol, const char* u = "dB")
    { return { n, std::fabs(obs - exp) <= tol, obs, exp, u }; }
    Check below (const char* n, double obs, double ceil, const char* u = "dB")
    { return { n, obs <= ceil, obs, ceil, u }; }
    Check above (const char* n, double obs, double floor_, const char* u = "dB")
    { return { n, obs >= floor_, obs, floor_, u }; }

    // Parameter setting helper (works with APVTS via setValueNotifyingHost on the underlying param).
    void setParam(Proc& p, const juce::String& id, float v)
    {
        if (auto* pp = p.apvts.getParameter(id)) pp->setValueNotifyingHost(v);
    }

    std::unique_ptr<Proc> makeProc()
    {
        auto p = std::make_unique<Proc>();
        p->setPlayConfigDetails(2, 2, kSR, kBlock);
        p->prepareToPlay(kSR, kBlock);
        return p;
    }

    int runAll()
    {
        // ---- T1: Gain +6 dB boosts RMS by 6 dB ----
        std::printf("\n=== Utility T1: Gain ===\n");
        {
            auto p = makeProc();
            setParam(*p, "gain", (float)Proc::gainToNorm(6.0));
            juce::AudioBuffer<float> buf(2, kBlock);
            fillSine(buf, 1000.0, 0.25);
            const double rmsIn = rms(buf, 0);
            runOnce(*p, buf);
            print(nearly("gain +6 dB yields +6 dB RMS",
                   dbFromRms(rms(buf, 0)) - dbFromRms(rmsIn), 6.0, 0.1));
        }

        // ---- T2: Mute silences both channels ----
        std::printf("\n=== Utility T2: Mute ===\n");
        {
            auto p = makeProc();
            setParam(*p, "mute", 1.0f);
            juce::AudioBuffer<float> buf(2, kBlock);
            fillSine(buf, 1000.0, 0.5);
            runOnce(*p, buf);
            print(below("mute output L <= -200 dB", dbFromRms(rms(buf, 0)), -200.0));
            print(below("mute output R <= -200 dB", dbFromRms(rms(buf, 1)), -200.0));
        }

        // ---- T3: Phase L inverts L only ----
        std::printf("\n=== Utility T3: Phase L ===\n");
        {
            auto p = makeProc();
            setParam(*p, "phaseL", 1.0f);
            juce::AudioBuffer<float> buf(2, kBlock);
            fillSine(buf, 1000.0, 0.5);
            // Capture a reference copy
            juce::AudioBuffer<float> ref(buf);
            runOnce(*p, buf);
            // L should be negated sample-for-sample; R unchanged
            bool okL = true, okR = true;
            for (int n = 0; n < kBlock; ++n) {
                if (std::fabs(buf.getSample(0, n) + ref.getSample(0, n)) > 1e-5f) okL = false;
                if (std::fabs(buf.getSample(1, n) - ref.getSample(1, n)) > 1e-5f) okR = false;
            }
            print({ "L is negated", okL, 0, 0, "" });
            print({ "R is unchanged", okR, 0, 0, "" });
        }

        // ---- T4: Channel Mode Swap ----
        std::printf("\n=== Utility T4: Channel Mode Swap ===\n");
        {
            auto p = makeProc();
            setParam(*p, "chanmode", 1.0f / 4.0f);   // Swap = index 1 of 5
            juce::AudioBuffer<float> buf(2, kBlock);
            // Put distinct patterns on L and R so we can verify swap
            for (int n = 0; n < kBlock; ++n) {
                buf.setSample(0, n, 0.3f);    // L = constant 0.3
                buf.setSample(1, n, -0.7f);   // R = constant -0.7
            }
            runOnce(*p, buf);
            const double outL = buf.getSample(0, kBlock / 2);
            const double outR = buf.getSample(1, kBlock / 2);
            print(nearly("swap: L becomes -0.7", outL, -0.7, 0.01, ""));
            print(nearly("swap: R becomes +0.3", outR,  0.3, 0.01, ""));
        }

        // ---- T5: Channel Mode Mono ----
        std::printf("\n=== Utility T5: Channel Mode Mono ===\n");
        {
            auto p = makeProc();
            setParam(*p, "chanmode", 4.0f / 4.0f);   // Mono = index 4 (last)
            juce::AudioBuffer<float> buf(2, kBlock);
            for (int n = 0; n < kBlock; ++n) {
                buf.setSample(0, n, 0.6f);
                buf.setSample(1, n, -0.2f);
            }
            runOnce(*p, buf);
            // Both channels should now be (0.6 + -0.2) * 0.5 = 0.2
            print(nearly("mono: L=mean(L,R)=0.2", buf.getSample(0, kBlock / 2), 0.2, 0.01, ""));
            print(nearly("mono: R=mean(L,R)=0.2", buf.getSample(1, kBlock / 2), 0.2, 0.01, ""));
        }

        // ---- T6: Width = 0 collapses to mono ----
        std::printf("\n=== Utility T6: Width 0 = mono ===\n");
        {
            auto p = makeProc();
            setParam(*p, "width", 0.0f);      // width = 0 (mono)
            juce::AudioBuffer<float> buf(2, kBlock);
            fillSine(buf, 1000.0, 0.5, /*invertR=*/true);  // anti-phase content is pure side signal
            runOnce(*p, buf);
            // Width=0 kills the side → output should be ~silent on both channels
            print(below("width 0 RMS on pure side signal is silent (< -40 dB)",
                        dbFromRms(rms(buf, 0)), -40.0));
        }

        // ---- T7: Mid Only kills side content ----
        std::printf("\n=== Utility T7: M/S Solo = Mid Only ===\n");
        {
            auto p = makeProc();
            setParam(*p, "mssolo", 0.5f);     // Mid = index 1 of 3 (norm 0.5)
            juce::AudioBuffer<float> buf(2, kBlock);
            fillSine(buf, 1000.0, 0.5, /*invertR=*/true);  // pure side
            runOnce(*p, buf);
            print(below("mid-only: pure-side input is silent (< -40 dB)",
                        dbFromRms(rms(buf, 0)), -40.0));
        }

        // ---- T8: Balance L = -1 silences L ----
        std::printf("\n=== Utility T8: Balance L = -1 ===\n");
        {
            auto p = makeProc();
            setParam(*p, "balL", 0.0f);   // -1 (silences L pre-width)
            setParam(*p, "width", 0.25f); // unity
            juce::AudioBuffer<float> buf(2, kBlock);
            fillSine(buf, 1000.0, 0.5);   // same on both channels
            runOnce(*p, buf);
            // After balance silences L, M/S encode of (0, sine) sends mid=R/2 to both channels, side=-R/2 to L +R/2 to R
            // Net: L gets only the mid, R gets mid+side = original
            // Simpler check: L RMS should drop by ~6 dB relative to R RMS
            const double dbL = dbFromRms(rms(buf, 0));
            const double dbR = dbFromRms(rms(buf, 1));
            print(above("balL=-1 reduces L significantly vs R (>3 dB delta)", dbR - dbL, 3.0));
        }

        // ---- T9: DC filter removes DC offset from a constant signal ----
        std::printf("\n=== Utility T9: DC Filter ===\n");
        {
            auto p = makeProc();
            setParam(*p, "dc", 1.0f);
            // Long buffer to let the one-pole settle
            const int bigN = (int)kSR; // 1 second
            juce::AudioBuffer<float> buf(2, bigN);
            for (int n = 0; n < bigN; ++n) { buf.setSample(0, n, 0.5f); buf.setSample(1, n, 0.5f); }
            // Process in chunks
            int done = 0;
            while (done < bigN) {
                const int n = std::min(kBlock, bigN - done);
                juce::AudioBuffer<float> chunk(buf.getArrayOfWritePointers(), 2, done, n);
                runOnce(*p, chunk);
                done += n;
            }
            // After settling, the DC filter output should be near zero
            const double tail = rms(buf, 0, bigN * 3 / 4);
            print(below("DC filter output on constant input → silence (< -40 dB)", dbFromRms(tail), -40.0));
        }

        // ---- T10: Bass Mono sums bass to mono, keeps highs stereo ----
        std::printf("\n=== Utility T10: Bass Mono ===\n");
        {
            auto p = makeProc();
            setParam(*p, "bassmono",    1.0f);
            setParam(*p, "bassmonofreq", 1.0f);  // top of range (~ 1 kHz) so 60 Hz is safely below
            // Input: low sine on L only (nothing on R).  Bass-mono should split 50/50 low to both.
            const int bigN = (int)kSR;
            juce::AudioBuffer<float> buf(2, bigN);
            for (int n = 0; n < bigN; ++n) {
                const double x = std::sin(2.0 * M_PI * 60.0 * n / kSR); // 60 Hz, well below 1 kHz xover
                buf.setSample(0, n, (float)(0.5 * x));
                buf.setSample(1, n, 0.0f);
            }
            int done = 0;
            while (done < bigN) {
                const int n = std::min(kBlock, bigN - done);
                juce::AudioBuffer<float> chunk(buf.getArrayOfWritePointers(), 2, done, n);
                runOnce(*p, chunk);
                done += n;
            }
            // After crossover: low band (60 Hz) summed-and-halved → both channels get half of original
            const double dbL = dbFromRms(rms(buf, 0, bigN / 2));
            const double dbR = dbFromRms(rms(buf, 1, bigN / 2));
            print(nearly("L ~= R with bass-mono on (within 1 dB)", dbR - dbL, 0.0, 1.0));
            print(above("R gained energy (> -20 dB)", dbR, -20.0));
        }

        std::printf("\n%s\n", failures == 0 ? "ALL UTILITY TESTS PASSED" : "UTILITY TESTS FAILED");
        return failures == 0 ? 0 : 1;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI _init; // APVTS + atomic helpers need JUCE message thread
    std::printf("Ferment Utility unit tests (SR=%.0f Hz, block=%d)\n", kSR, kBlock);
    return runAll();
}

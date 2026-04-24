// Ferment EQ biquad response tests.
//
// Verifies the RBJ cookbook coefficients in src-ferment/common/Biquad.h:
//   - Bell: peaks at its centre frequency with specified gain, settles to 0 dB far from centre
//   - LowShelf / HighShelf: correct asymptotic gain at 0/Nyquist
//   - LowCut / HighCut: unity well outside the cutoff, -3 dB near cutoff (for Q=0.707)
//   - Notch: deep null at centre frequency, unity far from it
//   - Identity: 0 dB everywhere

#include "../src-ferment/common/Biquad.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
    constexpr double kSR = 48000.0;
    using ferment::Biquad;

    struct Check
    {
        const char* name;
        double observed;
        double expected;
        double tol;
        bool   ok;
    };

    Check nearly(const char* name, double observed, double expected, double tol)
    {
        return { name, observed, expected, tol, std::fabs(observed - expected) <= tol };
    }
    Check below(const char* name, double observed, double ceiling)
    {
        return { name, observed, ceiling, 0.0, observed <= ceiling };
    }
    Check above(const char* name, double observed, double floor_)
    {
        return { name, observed, floor_, 0.0, observed >= floor_ };
    }

    void print(const Check& c)
    {
        std::printf("  %-54s observed=%+7.2f dB  %s\n", c.name, c.observed, c.ok ? "PASS" : "FAIL");
    }

    int runAll()
    {
        int fails = 0;
        auto check = [&](const Check& c) { print(c); if (!c.ok) fails++; };

        std::printf("\n=== Biquad: Bell +6 dB @ 1 kHz, Q=1 ===\n");
        {
            Biquad bq; bq.setBell(1000.0, 1.0, 6.0, kSR);
            check(nearly("response at 1 kHz ~= +6 dB",  bq.responseDb(1000.0, kSR),  6.0, 0.3));
            check(nearly("response at  50 Hz ~=  0 dB", bq.responseDb(50.0,   kSR),  0.0, 0.5));
            check(nearly("response at  20 kHz ~= 0 dB", bq.responseDb(18000., kSR),  0.0, 0.5));
        }

        std::printf("\n=== Biquad: Bell -12 dB @ 2 kHz, Q=2 ===\n");
        {
            Biquad bq; bq.setBell(2000.0, 2.0, -12.0, kSR);
            check(nearly("response at 2 kHz ~= -12 dB",  bq.responseDb(2000.0, kSR), -12.0, 0.5));
            check(nearly("response far below ~ 0 dB",    bq.responseDb(100.0,  kSR),   0.0, 0.5));
        }

        std::printf("\n=== Biquad: LowShelf +6 dB @ 200 Hz ===\n");
        {
            Biquad bq; bq.setLowShelf(200.0, 0.707, 6.0, kSR);
            check(nearly("DC-ish (30 Hz) ~= +6 dB",  bq.responseDb(30.0,  kSR),  6.0, 1.0));
            check(nearly("high (10 kHz) ~=  0 dB",    bq.responseDb(10000., kSR), 0.0, 0.5));
        }

        std::printf("\n=== Biquad: HighShelf -9 dB @ 8 kHz ===\n");
        {
            Biquad bq; bq.setHighShelf(8000.0, 0.707, -9.0, kSR);
            check(nearly("low (100 Hz) ~= 0 dB",        bq.responseDb(100.0,  kSR),  0.0, 0.5));
            check(nearly("top (18 kHz) ~= -9 dB",       bq.responseDb(18000., kSR), -9.0, 1.0));
        }

        std::printf("\n=== Biquad: LowCut @ 500 Hz, Q=0.707 ===\n");
        {
            Biquad bq; bq.setLowCut(500.0, 0.707, kSR);
            check(above ("passband 5 kHz >= -0.5 dB",   bq.responseDb(5000.0, kSR), -0.5));
            check(below ("stopband 50 Hz <= -20 dB",    bq.responseDb(50.0,   kSR), -20.0));
            check(nearly("at cutoff 500 Hz ~ -3 dB",    bq.responseDb(500.0,  kSR), -3.0, 1.0));
        }

        std::printf("\n=== Biquad: HighCut @ 3 kHz, Q=0.707 ===\n");
        {
            Biquad bq; bq.setHighCut(3000.0, 0.707, kSR);
            check(above ("passband 300 Hz >= -0.5 dB",  bq.responseDb(300.0,  kSR), -0.5));
            check(below ("stopband 18 kHz <= -20 dB",   bq.responseDb(18000., kSR), -20.0));
            check(nearly("at cutoff 3 kHz ~ -3 dB",     bq.responseDb(3000.0, kSR), -3.0, 1.0));
        }

        std::printf("\n=== Biquad: Notch @ 1 kHz, Q=5 ===\n");
        {
            Biquad bq; bq.setNotch(1000.0, 5.0, kSR);
            check(below ("null at 1 kHz <= -30 dB",     bq.responseDb(1000.0, kSR), -30.0));
            check(nearly("far away 200 Hz ~ 0 dB",      bq.responseDb(200.0,  kSR),  0.0, 0.5));
            check(nearly("far away 10 kHz ~ 0 dB",      bq.responseDb(10000., kSR),  0.0, 0.5));
        }

        std::printf("\n=== Biquad: Identity (unset band) ===\n");
        {
            Biquad bq; // default construct = identity
            check(nearly("20 Hz ~ 0 dB",    bq.responseDb(20.0,    kSR), 0.0, 0.001));
            check(nearly("1 kHz ~ 0 dB",    bq.responseDb(1000.0,  kSR), 0.0, 0.001));
            check(nearly("20 kHz ~ 0 dB",   bq.responseDb(20000.0, kSR), 0.0, 0.001));
        }

        std::printf("\n=== Biquad: Time-domain sanity (sine through Bell +6 dB @ 1 kHz, Q=1) ===\n");
        {
            Biquad bq; bq.setBell(1000.0, 1.0, 6.0, kSR);
            const int n = (int)kSR; // 1 second
            const double f = 1000.0;
            // process pure sine at 1 kHz, measure RMS
            double sumIn = 0.0, sumOut = 0.0;
            const int warmup = (int)(kSR * 0.1); // 100 ms
            for (int i = 0; i < n; ++i)
            {
                const double x = std::sin(2.0 * M_PI * f * i / kSR);
                const double y = bq.processL(x);
                if (i >= warmup) {
                    sumIn  += x * x;
                    sumOut += y * y;
                }
            }
            const double rmsIn  = std::sqrt(sumIn  / (n - warmup));
            const double rmsOut = std::sqrt(sumOut / (n - warmup));
            const double gainDb = 20.0 * std::log10(rmsOut / rmsIn);
            check(nearly("measured RMS gain at 1 kHz ~ +6 dB", gainDb, 6.0, 0.3));
        }

        std::printf("\n%s\n", fails == 0 ? "ALL BIQUAD TESTS PASSED" : "BIQUAD TESTS FAILED");
        return fails;
    }
}

int main()
{
    std::printf("Ferment EQ biquad unit tests (SR=%.0f Hz)\n", kSR);
    return runAll() == 0 ? 0 : 1;
}

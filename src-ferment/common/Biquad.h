#pragma once

// RBJ biquad cookbook — https://www.w3.org/TR/audio-eq-cookbook/
// Transposed Direct Form II topology.

#include <cmath>
#include <complex>

namespace ferment
{

struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double zL1 = 0.0, zL2 = 0.0, zR1 = 0.0, zR2 = 0.0;

    void reset() noexcept { zL1 = zL2 = zR1 = zR2 = 0.0; }

    inline double processL(double x) noexcept
    {
        const double y = b0 * x + zL1;
        zL1 = b1 * x - a1 * y + zL2;
        zL2 = b2 * x - a2 * y;
        return y;
    }

    inline double processR(double x) noexcept
    {
        const double y = b0 * x + zR1;
        zR1 = b1 * x - a1 * y + zR2;
        zR2 = b2 * x - a2 * y;
        return y;
    }

    // Identity filter (passthrough)
    void setIdentity() noexcept
    {
        b0 = 1.0; b1 = 0.0; b2 = 0.0; a1 = 0.0; a2 = 0.0;
    }

    // Normalise + store: given unnormalised cookbook (b0',b1',b2',a0,a1',a2')
    void setFromCookbook(double _b0, double _b1, double _b2,
                         double _a0, double _a1, double _a2) noexcept
    {
        const double inv = 1.0 / _a0;
        b0 = _b0 * inv; b1 = _b1 * inv; b2 = _b2 * inv;
        a1 = _a1 * inv; a2 = _a2 * inv;
    }

    // Peaking (bell)
    void setBell(double freq, double q, double gainDb, double sr) noexcept
    {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w = 2.0 * M_PI * freq / sr;
        const double cosw = std::cos(w);
        const double sinw = std::sin(w);
        const double alpha = sinw / (2.0 * q);
        setFromCookbook(
            1.0 + alpha * A,     -2.0 * cosw,      1.0 - alpha * A,
            1.0 + alpha / A,     -2.0 * cosw,      1.0 - alpha / A);
    }

    void setLowShelf(double freq, double q, double gainDb, double sr) noexcept
    {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w = 2.0 * M_PI * freq / sr;
        const double cosw = std::cos(w);
        const double sinw = std::sin(w);
        // S = 1 yields 12 dB/oct; we derive alpha from Q instead (cookbook alt form)
        const double alpha = sinw / (2.0 * q);
        const double twoSqrtAalpha = 2.0 * std::sqrt(A) * alpha;
        setFromCookbook(
            A * ((A + 1) - (A - 1) * cosw + twoSqrtAalpha),
            2 * A * ((A - 1) - (A + 1) * cosw),
            A * ((A + 1) - (A - 1) * cosw - twoSqrtAalpha),
            (A + 1) + (A - 1) * cosw + twoSqrtAalpha,
            -2 * ((A - 1) + (A + 1) * cosw),
            (A + 1) + (A - 1) * cosw - twoSqrtAalpha);
    }

    void setHighShelf(double freq, double q, double gainDb, double sr) noexcept
    {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w = 2.0 * M_PI * freq / sr;
        const double cosw = std::cos(w);
        const double sinw = std::sin(w);
        const double alpha = sinw / (2.0 * q);
        const double twoSqrtAalpha = 2.0 * std::sqrt(A) * alpha;
        setFromCookbook(
            A * ((A + 1) + (A - 1) * cosw + twoSqrtAalpha),
            -2 * A * ((A - 1) + (A + 1) * cosw),
            A * ((A + 1) + (A - 1) * cosw - twoSqrtAalpha),
            (A + 1) - (A - 1) * cosw + twoSqrtAalpha,
            2 * ((A - 1) - (A + 1) * cosw),
            (A + 1) - (A - 1) * cosw - twoSqrtAalpha);
    }

    void setLowCut(double freq, double q, double sr) noexcept
    {
        // RBJ HPF (low-cut)
        const double w = 2.0 * M_PI * freq / sr;
        const double cosw = std::cos(w);
        const double sinw = std::sin(w);
        const double alpha = sinw / (2.0 * q);
        setFromCookbook(
            (1.0 + cosw) * 0.5,  -(1.0 + cosw),   (1.0 + cosw) * 0.5,
            1.0 + alpha,         -2.0 * cosw,     1.0 - alpha);
    }

    void setHighCut(double freq, double q, double sr) noexcept
    {
        // RBJ LPF (high-cut)
        const double w = 2.0 * M_PI * freq / sr;
        const double cosw = std::cos(w);
        const double sinw = std::sin(w);
        const double alpha = sinw / (2.0 * q);
        setFromCookbook(
            (1.0 - cosw) * 0.5,  1.0 - cosw,      (1.0 - cosw) * 0.5,
            1.0 + alpha,         -2.0 * cosw,     1.0 - alpha);
    }

    void setNotch(double freq, double q, double sr) noexcept
    {
        const double w = 2.0 * M_PI * freq / sr;
        const double cosw = std::cos(w);
        const double sinw = std::sin(w);
        const double alpha = sinw / (2.0 * q);
        setFromCookbook(
            1.0,                 -2.0 * cosw,     1.0,
            1.0 + alpha,         -2.0 * cosw,     1.0 - alpha);
    }

    // Magnitude (dB) at a given frequency.
    double responseDb(double freq, double sr) const noexcept
    {
        using cd = std::complex<double>;
        const double w = 2.0 * M_PI * freq / sr;
        cd ejw  = std::polar(1.0, -w);
        cd ej2w = ejw * ejw;
        cd num = cd(b0, 0) + b1 * ejw + b2 * ej2w;
        cd den = cd(1.0, 0) + a1 * ejw + a2 * ej2w;
        const double mag = std::abs(num / den);
        return 20.0 * std::log10(mag + 1e-12);
    }
};

} // namespace ferment

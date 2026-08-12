/* ========================================
 *  ferment-policy — the ONE translator from analyzer targets to Ferment
 *  module values. Owned by Ferment Master (which applies them); Ferment
 *  Analyzer may render the same output as a hint panel. Nothing else in
 *  the codebase is allowed to map measurements to knobs.
 *
 *  The tables ARE the product voicings — the shipped Studio and Reel
 *  lanes and ladders, verbatim. They are versioned code, not
 *  documentation.
 * ======================================== */

#ifndef FERMENT_POLICY_H
#define FERMENT_POLICY_H

#include "AnalyzerCore.h"

namespace ferment::policy {

// Display units, exactly as printed on the plugin faces.
struct ChargeSettings {
    // Defaults = the studio base voicing: a default-constructed settings
    // struct must be a sane, neutral chain, never indeterminate (a garbage
    // staging gain from an uninitialised struct cost a debugging session).
    double compression = 5.5, attack = 4.5, release = 4.0, saturation = 4.5;
    int satMode = 1;        // 0 Mild / 1 Moderate / 2 Hot
    double character = 6.5; // knob 1..10
    int charMode = 2;       // 0 Fat / 1 Warm / 2 Bright
    int detectorHp = 1;     // 0 Off / 1 100 / 2 300
    double mixPct = 70.0;   // 0..100
};

struct EqSettings {
    double lowShelfDb = 0.0;   // 100-120 Hz band, <= 0 (sub trim ladder)
    double presenceDb = 0.0;   // 3 kHz bell
    double presenceQ = 0.9;
    double highShelfDb = 0.0;  // 7 kHz shelf (tilt delta, clamped 0..6)
};

struct ClipSettings {
    double ceilingDb = -1.0;   // limiter ceiling + 0.5
    double kneePct = 35.0, tiltPct = 50.0, biasPct = 0.0;
};

struct LimitSettings {
    double ceilingDbtp = -1.5;
    double attackMs = 60.0;
    double releaseScale = 1.0;
    bool truePeak = true;
};

struct ChainSettings {
    double stagingDb = 0.0;   // Utility gain before the chain
    double widthFactor = 1.0; // Utility width (1.0 = untouched)
    ChargeSettings charge;
    EqSettings eq;
    ClipSettings clip;
    LimitSettings limit;
    double targetLufs = -9.5; // what the loudness push should land on
};

// profileName: "studio" | "reel" (matches analyzer::Profile::name).
ChainSettings translate(const ferment::analyzer::Readout& r,
                        const ferment::analyzer::Profile& profile);

} // namespace ferment::policy

#endif

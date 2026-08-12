#include "FermentPolicy.h"

#include <algorithm>
#include <cstring>

namespace ferment::policy {

using ferment::analyzer::Readout;
using ferment::analyzer::Profile;

// The shipped Studio and Reel voicings, expressed in display units.
namespace {

struct ChargeLanes { ChargeSettings base, subMid, subHeavy; };

const ChargeLanes kStudioCharge = {
    /* base     */ {5.5, 4.5, 4.0, 4.5, 1, 6.5, 2, 1, 70.0},
    /* subMid   */ {5.5, 4.5, 4.0, 4.0, 1, 6.5, 2, 1, 65.0},
    /* subHeavy */ {5.5, 4.5, 4.0, 3.5, 0, 6.5, 2, 2, 60.0},
};
const ChargeLanes kReelCharge = {
    /* base     */ {6.5, 3.5, 3.5, 6.0, 2, 7.0, 2, 1, 85.0},
    /* subMid   */ {6.5, 3.5, 3.5, 4.5, 1, 7.0, 2, 1, 75.0},
    /* subHeavy */ {6.5, 3.5, 3.5, 4.5, 1, 7.0, 2, 1, 70.0},
};

const ClipSettings kStudioClip = {-1.0, 35.0, 50.0, 0.0};
const ClipSettings kReelClip   = {-0.5, 22.0, 55.0, 15.0};

const LimitSettings kStudioLimit = {-1.5, 60.0, 1.0, true};
const LimitSettings kReelLimit   = {-1.0, 45.0, 0.8, true};

const double kStudioPresence = 1.5;
const double kReelPresence   = 2.5;

} // namespace

ChainSettings translate(const Readout& r, const Profile& profile)
{
    const bool reel = std::strcmp(profile.name, "reel") == 0;
    const ChargeLanes& lanes = reel ? kReelCharge : kStudioCharge;

    ChainSettings out;
    /*  Clamped to the Stage knob's own range: against silence the drive delta
        is "-16 minus the gate floor" = +284 dB, and while the parameter layer
        would clamp it at write-back, the core applies these settings directly
        first.  A translator that can emit a value no knob can hold is wrong
        one layer earlier than where it explodes.  */
    out.stagingDb   = std::clamp(r.targets.driveDeltaDb, -24.0, 24.0);
    out.widthFactor = r.targets.widthFactor;

    out.charge = r.subClass == Readout::SubHeavy  ? lanes.subHeavy
               : r.subClass == Readout::SubLeaning ? lanes.subMid
                                                   : lanes.base;

    out.eq.lowShelfDb  = r.targets.subTrimDb;
    out.eq.presenceDb  = reel ? kReelPresence : kStudioPresence;
    out.eq.presenceQ   = 0.9;
    out.eq.highShelfDb = r.targets.tiltDeltaDb;

    out.clip  = reel ? kReelClip : kStudioClip;
    out.limit = reel ? kReelLimit : kStudioLimit;
    out.targetLufs = profile.targetLufs;
    return out;
}

} // namespace ferment::policy

# Ferment Limit — dual-stage true-peak limiter

Limit is a lookahead brickwall with the architecture every loved mastering
limiter converges on: **two stages, not one envelope**. A transient stage
(sliding-minimum gain bound over a 2 ms window, smoothed by cascaded box
filters — mathematically incapable of overshoot, releases instantly by
construction) rides on top of a sustain stage (hold + crest-factor
auto-release). A passing hit dips and lets go; sustained loudness is held
by the slow stage — so short events never re-pump the level and sustained
bass never "breathes".

## Signal flow

```
gain -> detector (per-channel |x| + partial transient link;
        True Peak: 4x polyphase windowed-sinc estimate, group-delay aligned)
  -> transient bound (sliding min -> cascaded unequal box smoothing)
  -> sustain envelope (attack knob = hand-off speed, 25 ms hold,
     crest-factor auto-release x Release scale)
  -> total = max(transient, sustain) -> delayed dry x gain -> output trim
```

## Parameters (ABI order, kParamA..H)

| # | Param | Range | Notes |
|---|---|---|---|
| A | Gain | 0..+24 dB | drive into the fixed ceiling |
| B | Ceiling | −24..0 dBFS | dBTP when True Peak is on |
| C | Attack | 10..300 ms | NOT an attack time: how quickly the sustain stage takes over from the transient stage (long = punchy, short = dense) |
| D | Release | 0.25x..4x | scales the auto-release; 1x = neutral |
| E | Trans Link | 0..100 % | transient stereo linking (release stage is always fully linked) |
| F | True Peak | Off / On | ITU-style 4x sidechain detection + internal 0.3 dB margin |
| G | Delta | Off / On | monitor what is removed |
| H | Output | ±24 dB | |

Latency: 107 samples at 48 kHz (reported to host). Meter: `meterGrDb()` —
gain reduction in the last block.

## Usage notes

- Mastering GR sweet spot: 2–4 dB on the loud sections. Feed it from
  Ferment Clip so the fastest energy never reaches the limiter.
- True-peak contract: holds the ceiling within +0.05 dB on programme
  material; adversarial near-Nyquist signals can reach +0.25 dB (the same
  behaviour the reference commercial limiters measure).

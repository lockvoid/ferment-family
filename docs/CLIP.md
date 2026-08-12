# Ferment Clip — ADAA mastering clipper

Clip shaves transient peaks so the limiter after it works 1–3 dB instead of
6 — the modern loud-master workflow (clip first, then limit). The core is a
morphing-knee waveshaper run inside 4x oversampling with first-order ADAA
(antiderivative antialiasing) and droop compensation: alias energy sits
~23 dB below a naive clipper at the same oversampling, and the linear
region is transparent to within thousandths of a dB.

## Signal flow

```
input trim -> 4x oversample (min-phase polyphase halfband — no pre-ring on
              clipped transients)
  -> tilt pre-emphasis -> ADAA morphing-knee clipper (+ bias) ->
     exact-inverse tilt de-emphasis
  -> downsample -> auto gain -> mix / delta -> output trim
  -> safety hard clip at the ceiling (catches decimation overshoot)
```

## Parameters (ABI order, kParamA..I)

| # | Param | Range | Notes |
|---|---|---|---|
| A | Input | ±24 dB | drive into the ceiling |
| B | Ceiling | −24..0 dBFS | the clip point (sample-peak; true-peak safety belongs to the limiter after) |
| C | Knee | 0..100 % | hard → soft morph; bit-transparent below the knee |
| D | Tilt | 0..100 % | clip in a spectrally tilted domain: bass stops dominating the transfer curve, mid/high detail survives heavy drive |
| E | Bias | ±100 % | asymmetric ceilings → even harmonics ("tube" edge); DC-blocked |
| F | Mix | 0..100 % | parallel clip |
| G | Output | ±24 dB | |
| H | Auto Gain | Off / On | loudness-matched makeup (RMS), defeats louder-is-better bias while dialling drive |
| I | Delta | Off / On | monitor only what is being removed — drive until delta is just transient ticks |

Latency: 6 samples at 48 kHz (reported to host). Meter: `meterShaveDb()` —
peak dB shaved in the last block (3–9 dB on drum transients is the normal
mastering range).

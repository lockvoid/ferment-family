# Ferment Charge — high octane levelling compressor

Charge is a programme leveller with an integrated saturator and spectral
shaper — one knob of "more", tuned for density rather than surgical control.
It behaves like a vari-mu-style unit: there is **no threshold knob and no
knee**. The static curve is a smooth softplus, and the Compression knob
sweeps operating point and makeup together, so turning it up simultaneously
levels harder and stays loudness-consistent.

## Signal flow

```
input trim
  -> detector (HP filter -> sidechain select -> stereo-mode link)
  -> levelling gain cell   (symmetric; detector ripple supplies the odd
                            harmonics — there is deliberately no waveshaper
                            in the cell)
  -> saturation            (Mild / Moderate / Hot)
  -> character             (low shelf + mid bell + high shelf)
  -> mix -> output trim
```

## Parameters (ABI order, kParamA..N)

| # | Param | Range | Notes |
|---|---|---|---|
| A | Input | ±20 dB | drive into the leveller; the primary "how hard" control together with Compression |
| B | Compression | 1..10 | levelling depth; sweeps threshold *and* makeup — output loudness stays roughly constant while density rises |
| C | Attack | 1..10 | ascending = slower; slower attack passes more transient |
| D | Release | 1..10 | ascending = slower |
| E | Saturation | 1..10 | amount into the saturation stage |
| F | Sat Mode | Mild / Moderate / Hot | Mild is **asymmetric** (even harmonics, tube-like — the gentlest on sub-heavy material); Moderate/Hot are symmetric (odd harmonics), Hot steepest |
| G | Character | 1..10 | amount of spectral shaping |
| H | Char Mode | Fat / Warm / Bright | Fat lifts lows, Warm tames the top, Bright lifts 2.4–12 kHz (the small-speaker band) |
| I | Detector HP | Off / 100 Hz / 300 Hz | keeps bass out of the detector: Off lets the kick pump the bus, 300 Hz keeps the low end from eating gain reduction |
| J | Stereo Mode | Link / Dual Mono / M-S | M-S levels mid and sides independently — sides open up instead of ducking with the centre |
| K | Sidechain | Off / On | external detector input (inputs 3/4) |
| L | SC Gain | ±20 dB | sidechain trim |
| M | Mix | 0..100 % | parallel (NY) blend — heavy settings at 50–70 % mix give density with intact transients |
| N | Output | ±20 dB | |

## Metering

`meterGrDb()` — current gain reduction in dB (max of the two lanes, so the
number means the same thing in every stereo mode). Shown on the editor's
needle meter.

## Usage notes

- **Sub-heavy programme** (electronic bass music): Mild + Detector HP 300 +
  parallel mix. Symmetric saturation on dominant sub reads as
  intermodulation "roar", especially where the midrange is sparse.
- **Punch**: slower Attack + Mix below ~70 %. The dry path carries the hit;
  the wet path carries the weight.
- **Pump** (deliberate): Detector HP Off, faster Release — the kick drives
  the cell.
- Zero added latency; safe on tracking chains.

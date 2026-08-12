# Ferment Glue — SSL-style bus compressor

Glue is a classic quad-VCA-style bus compressor: the "make a mix sound like
a record" device. Its character comes from the feedback-style detector and
the famous programme-dependent Auto release — set slow attack, Auto
release, 2–4 dB of needle movement, and the mix breathes as one thing.

## Parameters (ABI order, kParamA..J)

| # | Param | Range | Notes |
|---|---|---|---|
| A | Threshold | dB | where the detector starts working |
| B | Ratio | 2:1 / 4:1 / 10:1 | 2:1 glue, 4:1 control, 10:1 limiting-ish |
| C | Attack | fast..slow (ms) | slow settings let transients through — the punch control |
| D | Release | 1.2 s … 0.1 s … **Auto** | Auto is the programme-dependent mode that made this topology famous: fast after hits, slow on sustain — the "breathing" |
| E | Makeup | dB | post gain |
| F | Range | dB | maximum gain reduction — a ceiling on how hard the compressor may work; small Range values keep extreme settings musical |
| G | Dry/Wet | 0..100 % | parallel compression without an aux |
| H | Sidechain | Off / On | external detector input |
| I | SC HPF | 20..500 Hz (log) | keeps bass from pumping the bus — 60–120 Hz is the usual spot |
| J | Clip | Off / On | soft-clip output ceiling — a safety/character stage after makeup |

## Metering

`meterGrDb()` — gain reduction in dB, driving the editor's analog needle
(GR scale, rest at 0, deflects left).

## Usage notes

- **The glue preset**: Ratio 2:1 or 4:1, Attack slow (10–30 ms), Release
  Auto, Threshold until the needle touches 2–4 dB on the loud phrases,
  Makeup to taste. If it stops bouncing, the attack is too fast or the
  needle sits too deep.
- **Groove material**: Release fast-ish or Auto, SC HPF ~90 Hz so the kick
  doesn't own the detector; let the needle *recover between beats* — that
  recovery is the pump that reads as movement.
- **Drum bus smash**: 10:1, fast attack, Dry/Wet ~40 % — NY compression in
  one plugin.
- Zero added latency.

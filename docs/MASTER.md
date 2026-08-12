# Ferment Master — the whole chain, one Learn

Master is the family's mastering chain in a single plugin: five modules in
the validated order, each on its own card with its own bypass and meter,
and one button that sets all of them from ten seconds of the track.

## Signal flow

```
STAGE                 CHARGE            TONE              CLIP        LIMIT
gain + HP 24 Hz   →   levelling     →   low shelf     →   ADAA    →   dual-stage
+ width               compressor        presence bell     clipper     true-peak
                      + saturation      high shelf        (+ push)    limiter
```

The loudness push learned in phase 2 is applied at the Clip input — gain
before the final stage, where the clipper and limiter can catch what it
raises. Clip's ceiling rides ~0.5 dB above Limit's, same as the manual
chain.

Every module bypass is latency-matched and crossfaded (10 ms), so
engaging one mid-playback neither clicks nor moves the plugin's reported
latency — the DAW never has to re-plan the graph.

## Learn

Press **LEARN** and play the loudest, densest section.

- **Phase 1 — listening (10 s).** Measures the *source* at the plugin
  input — before the chain, so the current knob positions cannot
  contaminate it. At the end, the measurement is translated into module
  values: staging to the drive point, a saturation lane chosen by sub
  share (sub-heavy material gets the gentle asymmetric mode, a detector
  high-pass and a drier mix), tone from measured tilt, ceilings from the
  profile.
- **Phase 2 — setting (~4 s).** Keeps measuring the now-coloured signal
  after Tone and sets the loudness push = profile target minus what it
  heard. Phase 2 counts only audible material: stop the transport and it
  freezes, then finishes when playback resumes.

Everything Learn decides lands on ordinary visible parameters, written
inside balanced host gestures — undoable, recorded as automation, saved
with the session and re-applied on reload. There is no hidden state to
lose.

A Learn that heard nothing (silence, or a signal below the −70 LUFS
gate) learns nothing and returns to idle.

Second presses while a Learn runs are ignored; a fresh Learn afterwards
re-measures from scratch and, on the same material, lands on the same
values — the whole path is deterministic.

## Parameters

| Module | Parameters |
|---|---|
| Stage | Gain ±24 dB · Width 0–100 % |
| Charge | Compression, Attack, Release, Saturation, Character 1–10 · Sat Mode (Mild/Moderate/Hot) · Voice (Fat/Warm/Bright) · SC HP (Off/100/300) · Mix 0–100 % |
| Tone | Low shelf −12..0 dB (100 Hz) · Presence ±6 dB + Q (3 kHz) · High shelf 0..6 dB (7 kHz) |
| Clip | Ceiling · Knee · Tilt · Bias |
| Limit | Ceiling (dBTP) · Attack · Release · True Peak on/off |
| — | Push ±24 dB (written by Learn, drivable and automatable) · Profile (Studio/Reel) · Learn (momentary) · five bypasses |

## Profiles

- **STUDIO** — −9.5 LUFS at −1.5 dBTP. Monitors, big systems, streaming
  under normalisation.
- **REEL** — −8.5 LUFS at −1.0 dBTP. Phones, compressed delivery, the
  feed. A hotter push, a brighter tilt target, harder ceilings.

## Practical notes

- Learn measures what plays *during* the Learn. Ten seconds of intro
  gives you an intro's master — press it on the drop.
- Expect the result to land within a fraction of a dB of target; verify
  with Percept's RESULT mode and trim the **Push** knob if you want it
  hotter — it is a real parameter, not a readout.
- The header shows IN and OUT as gated LUFS with true peak; per-module
  needles show Charge GR, Clip shave and Limit GR while you play.
- Non-finite input (a NaN from an upstream plugin) is sanitised at the
  boundary: it passes as a dropout that ends, never a poisoned chain.

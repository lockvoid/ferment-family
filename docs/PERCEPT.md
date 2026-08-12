# Ferment Percept — the ear before the chain

Percept measures a track the way a mastering chain needs it measured, and
says what it hears: numbers in audio units, verdicts as lamps, and the
targets a master should hit. It changes nothing — the passthrough is
bit-exact by construction — and it never blocks the audio thread to draw.

## What it measures

| Lane | Meaning |
|---|---|
| LUFS-I | BS.1770 integrated loudness, two-stage gated, since reset |
| LUFS-S | short-term loudness, 3 s window |
| TRUE PEAK | 4x-interpolated inter-sample peak; running max + 3 s recent |
| CREST | 3 s peak minus RMS — how much transient is left |
| TILT | mean energy 2–12 kHz minus 100–500 Hz — dark vs bright, in dB |
| MONO LOSS | short-term stereo minus the mono fold — what a phone throws away |

**BAND SHARE** splits energy into six bands (20–60, 60–150, 150–400,
400–2k, 2–6k, 6–16k) on a fixed scale, so a sub-heavy track *looks*
sub-heavy instead of being renormalised into innocence.

## Verdicts

A rail of lamps, lit when the reading crosses the same thresholds the
targets use — silence lights nothing, and a track that plays and then
stops keeps its verdicts:

- **SUB HEAVY / SUB LEANING** — sub share over 40 % / 15 %
- **DARK / BRIGHT** — spectral tilt vs the profile's tilt target
- **PRE-CLIPPED** — true peak over 0 dBTP on the *source*
- **ALREADY DENSE** — crest under 9 dB: little mastering headroom left
- **MONO FRAGILE** — mono fold loses more than 2 dB

## Targets

The right column is what to do about it, in plain audio units: staging
gain to the drive point, gain to the loudness target, tilt delta, a
low-shelf trim ladder from sub share, a width ladder from mono loss, and
the profile ceiling. **CHAIN HINT** renders the same policy Ferment
Master applies — module by module — so you can set a manual chain from
it, or just let Master's Learn do it.

## Modes and profiles

- **SOURCE** — measure the mix; the hint shows the chain it wants.
- **RESULT** — measure the *master*; the hint becomes a verification:
  loudness vs target, true peak vs ceiling, codec-overshoot risk, crest.
- **STUDIO** (−9.5 LUFS, −1.5 dBTP) / **REEL** (−8.5 LUFS, −1.0 dBTP) —
  conservative for monitors and big systems; aggressive for phones and
  compressed delivery.
- **RESET** clears the integrated measurement (momentary, automatable).

## Practical notes

- Loudness readings need a few seconds; the spectrum wants signal, not
  silence — six dashes mean "keep playing".
- In RESULT mode, expect ~1 dB of true-peak overshoot after lossy
  encoding at social-media loudness. The AAC RISK row exists because a
  master that measures clean can still overshoot once a codec has
  reconstructed it.

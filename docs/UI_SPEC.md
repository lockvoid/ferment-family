# ferment_ui — strict spec for the implementation fleet

Status: NORMATIVE. Deviations require lead sign-off before code is written.
Lead reviews every PR against the acceptance criteria below; "close enough"
is not a pass state.

## 0. Mission

A small in-house JUCE component kit shared by all Ferment plugins, matching
the shipped brand mock (`ferment-landing` → `KnobRackMock.astro`, the
"GLUE BLUE" rack). Six components, vector-only, zero external dependencies.
This replaces the LookAndFeel-override approach for everything the
LookAndFeel cannot express (EQ graph, needle meters).

## 1. Hard constraints (violating any = PR rejected)

1. **DSP is untouchable.** Nothing under `src-ferment/*/Ferment*.{h,cpp}`
   (the pure-DSP layer) may change. UI reads meters via the existing
   `meter*()` accessors and parameters via APVTS only.
   **Sole sanctioned exception** (lead-approved per instance): adding a
   read-only `double meter*() const noexcept` accessor over existing
   private state, following the `FermentLimit::meterGrDb()` pattern — no
   new state, no audio-path change, no param change.
   Approved to date: `FermentGlue::meterGrDb()`,
   `FermentCharge::meterGrDb()`, the `FermentEqProcessor` spectrum FIFO
   (wrapper layer).
2. **Param ABI is frozen.** No renaming APVTS IDs, no enum reordering, no
   new parameters. UI adapts to DSP, never the reverse.
3. **No dependencies.** JUCE only. No fetched fonts, no image assets except
   one optional wood texture PNG ≤ 32 KB (procedural preferred).
4. **Vector-only rendering** via juce::Graphics/Path. Every component must
   look correct at 1x/1.25x/1.5x/2x scale (hosts rescale editors).
5. **No allocations or locks on the audio thread.** Meter data crosses via
   the existing atomics/polling pattern (editor Timer at 15–30 Hz).
6. **C++17, JUCE 8.0.4**, `juce::Font(juce::FontOptions(...))` API (the
   deprecated Font ctors used in WarmLookAndFeel are NOT to be copied).
7. Module layout: `src-ferment/ui/` as a JUCE-style module `ferment_ui`,
   one header + one cpp per component, no cross-component includes except
   `FermentTheme.h`.

## 2. Design tokens (`FermentTheme.h`) — single source of truth

Extracted from the brand mock; these exact values, named exactly:

```
namespace ferment::theme {
  // chassis / face
  bg          = 0xFF050505;   // page black
  faceTop     = 0xFF181210;   // panel gradient top
  faceBottom  = 0xFF0d0908;   // panel gradient bottom
  faceEdge    = 0xFF130e0b;
  // wood accents (live tokens only; the full brand palette lives in the
  // landing repo — woodMid/woodGrainDk were dropped with the rails)
  woodLight   = 0xFFd9b98a;
  woodDark    = 0xFF3a2c20;
  woodShadow  = 0xFF2b2118;
  // accents
  amber       = 0xFFE8902A;   // the ONE accent: arcs, values, needles
  labelDim    = 0x998a7666;   // knob captions (dim warm grey)
  labelCream  = 0xFFEFE6D8;   // FERMENT wordmark
  // typography: monospaced, ALL-CAPS captions, letter-spacing ~0.12em.
  // Use juce default monospace (no bundled fonts in v1).
}
```

Rule: no other colours anywhere. If a component seems to need one, it
doesn't — take it to the lead.

## 3. Components

### 3.1 `FermentKnob` (replaces per-editor setupKnob copies)

- Rotary, arc 135°→405° (matches current editors), amber value arc over a
  dark track arc, amber tick on the cap, dark cap with subtle radial shade.
- Value string under the knob (amber, mono, 11 px logical), caption under
  it (labelDim, bold mono 10 px, ALL CAPS, tracked).
- API: `FermentKnob(apvts, paramID, caption, formatter)` — attachment
  owned internally; `formatter: std::function<juce::String(double norm)>`.
- Behaviour: vertical+horizontal drag, double-click = reset to default,
  shift-drag = fine (10x), mouse-wheel steps.
- Acceptance: drop-in replaces `setupKnob` in the Clip editor with zero
  layout change; screenshot at 2x matches the mock's knob geometry
  (arc thickness ratio, cap/arc radius ratio) within eyeball tolerance
  approved by lead.

### 3.2 `NeedleMeter` (GR / VU, analog)

- Lit amber face, arc scale with dB ticks (configurable range, e.g.
  0..-12 GR), **black needle** (lead ruling: an amber needle on the lit
  face is invisible; backlit VUs use a dark pointer), subtle glass
  highlight.
- Ballistics INSIDE the component (input = instantaneous dB value from the
  processor's meter accessor): attack 50 ms, release 300 ms, both
  exponential; needle never steps visibly at 30 Hz polling.
- Modes: `GR` (rest at 0 right, deflect left) and `Level` (rest left).
- API: `NeedleMeter(std::function<double()> read, Mode, Range)`; owns its
  Timer.
- Acceptance: Glue editor shows GR needle; feeding a 4 dB square-wave GR
  pattern produces smooth needle motion (no zipper) in a screen recording.

### 3.3 `EqGraphEditor` (the big one — for Ferment EQ)

- Log-frequency 20 Hz..20 kHz, dB axis ±18, grid lines faceEdge.
- Response curve: sum of `ferment::Biquad::responseDb()` over the 8 bands
  (the DSP header already provides this — do NOT reimplement biquad math),
  amber, 2 px, with soft amber glow fill to the zero line at 12% alpha.
- Band nodes: circles at (freq, gain); drag = freq/gain; mouse-wheel or
  vertical drag with Cmd = Q; double-click = toggle band enable;
  right-click = band menu (type, reset). Active node grows + shows a
  tooltip `1.2 kHz  +3.5 dB  Q 1.4`.
- All edits go through APVTS `setValueNotifyingHost` with proper
  begin/endChangeGesture — automation-safe. Params follow FermentEq's
  existing 5-per-band stride ABI.
- Spectrum underlay: post-EQ spectrum, 2048 FFT, 30 Hz update, faceEdge→
  amber gradient at 20% alpha; data via a lock-free FIFO added in the
  PROCESSOR (wrapper layer — allowed; DSP layer — not).
- Knob row under the graph uses FermentKnob for the selected band.
- Acceptance: bit-exact parameter round-trip (drag node → APVTS → DSP →
  curve redraw matches `responseDb` within 0.1 dB); automation recorded in
  a DAW plays back moving the nodes; 60 fps drag on an M-series laptop.

### 3.4 `HeaderBar`

- Left: `FERMENT` wordmark (cream, bold mono 18 px). Right: product
  subtitle in amber, e.g. `CHARGE / HIGH OCTANE LEVELLING COMPRESSOR`
  (subtitle strings come from the lead, one per plugin).
- Optional right-side meter slot (Clip's SHAVE readout, Limit's GR text)
  — component slot, not hardcoded text.

### 3.5 `ChassisPanel`

- Panel: vertical gradient faceTop→faceBottom, 2 px faceEdge border,
  1 px inner highlight, **square corners** (lead ruling: the OS rounds the
  plugin window; rounded panel corners left a dark crescent in every
  corner). Corner shaping where needed uses `theme::squircle()`.
- `WoodRails` was REMOVED from the design (lead direction, 2026-08-12):
  no wood cheeks in shipped editors. Do not reintroduce without a new
  ruling.

### 3.6 Migration of existing editors

Order: Clip → Limit → Glue (+NeedleMeter) → Charge → Utility → EQ
(+EqGraphEditor). One PR per editor. Old `WarmLookAndFeel` dies when the
last editor migrates — not before.

## 4. Fleet workflow rules

- One component (or one editor migration) per track/PR. No drive-by edits.
- Every PR: builds all plugin targets + all tests green
  (`cmake --build build && ctest --test-dir build`).
- Visual work: attach 1x and 2x screenshots of the Standalone build to the
  PR (the Standalone target exists for every plugin).
- The lead owns: theme token changes, any API change to a merged
  component, anything touching `src-ferment/*/Ferment*.{h,cpp}` (which
  should be nothing), subtitle copy.
- Reference mock: `ferment-landing/src/pages/cuts/_components/KnobRackMock.astro`
  — when in doubt, the mock wins over taste.

## 5. Explicit non-goals (v1)

- No resizable-editor persistence, no theming/skins, no bundled fonts,
- no GPU/OpenGL contexts, no animation framework, no preset browser UI.

## 6. Backlog (ruled, not scheduled)

- `PeakHoldMeter` for Clip/Limit: their meters are per-block peaks, which
  a 50 ms needle under-reads — a peak-hold bar is the right instrument.
- Utility's private `ToggleLookAndFeel` becomes a kit component the day a
  second editor needs toggles (rule of two).

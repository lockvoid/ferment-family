# ferment_ui

The Ferment family's JUCE component kit. Vector-only, JUCE-only, theme tokens
only. Built against `ferment-family/docs/UI_SPEC.md`; every plugin in
`src-ferment/` links it as `ferment::ferment_ui`.

## Components

| Component | Role | Owns a Timer | Notes |
|---|---|---|---|
| `FermentTheme.h` | Colour, typography and shape tokens; `squircle()` | — | The only place in the module that names a colour |
| `ChassisPanel` | Full-bleed rack face, square corners, `layoutFrame()` | — | Mouse-transparent; defines the family's editor margins once |
| `HeaderBar` | Wordmark, product subtitle, borrowed right-hand slot | — | Slot held as a `SafePointer`, so a slot that dies first reads as absent |
| `FermentKnob` | The family rotary + APVTS attachment + grid layout | — | Text entry and arrow-key nudge per UI_SPEC §3.1/§7 |
| `FermentToggle` | On/off pill, optional bool-parameter attachment | — | Paints itself rather than carrying a LookAndFeel |
| `NeedleMeter` | Analog GR / level dial with ballistics inside | yes, 30 Hz | Lit face cached to an image; only the needle redraws |
| `EqGraphEditor` | EQ curve, draggable band nodes, spectrum underlay | yes | The big one; Ferment EQ only |
| `MeterStrip` | Labelled measurement lanes with bar, value and 3 s sparkline | — | Allocation-free paint and push; history ring sized at construction |
| `VerdictChips` | Wrapped run of capsules, lit when a verdict holds | — | Carries no thresholds; captions and states are handed in |
| `TargetList` | Label / value rows, signed where sign is meaning | — | Values formatted into a stack buffer, committed only on change |
| `ModuleCard` | Titled card with a control row, bypass slot and meter slot | — | Slots take plain Components, like `HeaderBar`'s |
| `LearnButton` | Ferment Master's hero control, four visual states | only while flashing | Told its phase; owns no policy |

## House rules

- One header and one cpp per component. No cross-component includes except
  `FermentTheme.h` — a component that needs to compose others takes them as
  plain `juce::Component` slots.
- `juce::Font(juce::FontOptions(...))` only; the deprecated `Font` constructors
  are not used anywhere here.
- Anything on a poll path formats into a `char` buffer and compares before
  committing to a `juce::String`. `juce::String` has no small-string
  optimisation, so building one per frame is an allocation per frame — see
  `tests/analyzerplug_test.cpp`, which measures this and fails if it regresses.
- **Paint is not allocation-free and cannot be.** JUCE 8 shapes text inside
  `Graphics::drawText`, which costs 8 allocations per call whatever the caller
  does. What a component owes is that its *drawing* adds nothing on top of its
  labels, and that the per-frame cost does not grow — both measured in
  `tests/ferment_ui_test.cpp` against a one-`drawText` control.
- `ferment_ui.cpp` `#include`s the component `.cpp` files. `src-ferment/
  CMakeLists.txt` declares them as `OBJECT_DEPENDS` so the Makefile generator
  rebuilds the unity TU when one changes; without it you get stale binaries and
  no warning.

## Regenerating the visuals

```
cmake --build <build-dir> --target ferment-screenshots   # screenshots/, 1x + 2x
cmake --build <build-dir> --target ferment_ui_gallery    # live design-review app
```

Screenshots are byte-deterministic; if one churns between runs with no code
change, something in an editor is time-dependent rather than state-dependent.

They are a weak regression test for anything that only shows in motion. The
Analyzer shot is taken after the meters have settled, so its sparkline is a flat
line: an off-by-one in the history ring that reversed the newest and oldest
samples moved not one pixel of `analyzer.png`. Component behaviour belongs in
`ferment_ui_test`.

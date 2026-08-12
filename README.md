# Ferment Family

The Ferment plugin suite — six plugins, one signal-chain philosophy:

| Plugin | Role | Docs |
|---|---|---|
| Ferment Utility | gain / phase / width / bass-mono housekeeping | |
| Ferment Glue | SSL-style bus compressor | [docs/GLUE.md](docs/GLUE.md) |
| Ferment Charge | high octane levelling compressor (+ saturation) | [docs/CHARGE.md](docs/CHARGE.md) |
| Ferment EQ | 8-band EQ | |
| Ferment Clip | ADAA mastering clipper (knee / tilt / bias) | [docs/CLIP.md](docs/CLIP.md) |
| Ferment Limit | dual-stage true-peak limiter | [docs/LIMIT.md](docs/LIMIT.md) |

## The look

![Ferment Glue](screenshots/glue.png)

![Ferment EQ](screenshots/eq.png)

| | |
|---|---|
| ![Charge](screenshots/charge.png) | ![Clip](screenshots/clip.png) |
| ![Limit](screenshots/limit.png) | ![Utility](screenshots/utility.png) |

Screenshots are generated deterministically:
`cmake --build build --target ferment-screenshots`.

Dual-layer architecture: pure C++ DSP (`src-ferment/<plugin>/Ferment*.{h,cpp}`,
no JUCE — vendored by Cuts iOS) + JUCE wrapper/editor per plugin
(VST3 / AU / CLAP / Standalone).

## The frozen param ABI

Downstream pipelines and the iOS vendor copy address parameters **by
index** (`kParamA..` aliases in every DSP header). Indices never reorder;
new parameters append only. Plugin codes (`FmGl`…) and APVTS IDs are
likewise frozen — DAW sessions depend on them.

## Build

    cmake -B build && cmake --build build -j 8
    ctest --test-dir build          # DSP + processor tests

Requires network on first configure (CPM fetches JUCE 8.0.4 +
clap-juce-extensions). `COPY_PLUGIN_AFTER_BUILD=TRUE` installs into the
user plugin folders.

## Installer (macOS)

    scripts/installer_mac/make_ferment_installer.sh VERSION build out res

Signing/notarization via env: `MAC_SIGNING_CERT` (Developer ID
Application), `MAC_INSTALLING_CERT` (Developer ID Installer),
`MAC_SIGNING_ID` / `MAC_SIGNING_TEAM` / `MAC_SIGNING_1UPW` (notarytool).
Unset = unsigned dev build.

## Docs

- `docs/UI_SPEC.md` — normative spec for the `ferment_ui` component kit
- `docs/{CHARGE,GLUE,CLIP,LIMIT}.md` — per-plugin technical docs
- `tools/` — screenshot renderer + UI gallery app

Provenance: product tree migrated from the R&D sandbox with history
(`git filter-repo`); release binaries null-tested against the
pre-migration builds (Clip/Limit bit-exact, Charge within 2 ULP float32).

#!/bin/bash
# Builds a single macOS installer for the Ferment Family suite (Glue, EQ,
# Utility today; new plugins added via FERMENT_PLUGINS below).  The installer
# is a unified .pkg (with per-plugin selectable install choices) wrapped in
# a DMG.
#
# Usage:
#   make_ferment_installer.sh VERSION BUILD_DIR OUTPUT_DIR RESOURCES_DIR
#
# Optional env vars for signing + notarisation (if any is missing, skip that
# step):
#   MAC_SIGNING_CERT      - Developer ID Application cert name
#   MAC_INSTALLING_CERT   - Developer ID Installer cert name
#   MAC_SIGNING_ID        - Apple ID email (for notarytool)
#   MAC_SIGNING_TEAM      - Team ID
#   MAC_SIGNING_1UPW      - App-specific password
set -euo pipefail

VERSION="$1"
BUILD_DIR="$2"
OUTPUT_DIR="$3"
RESOURCES_DIR="$4"

TMPDIR="$BUILD_DIR/ferment-installer-tmp"
rm -rf "$TMPDIR"
mkdir -p "$TMPDIR" "$OUTPUT_DIR"

# Plugins to package: name | pkg-safe slug | reverse-domain id base
FERMENT_PLUGINS=(
  "Ferment Glue|ferment-glue|com.ferment.dsp.glue"
  "Ferment EQ|ferment-eq|com.ferment.dsp.eq"
  "Ferment Utility|ferment-utility|com.ferment.dsp.utility"
  "Ferment Charge|ferment-charge|com.ferment.dsp.charge"
  "Ferment Clip|ferment-clip|com.ferment.dsp.clip"
  "Ferment Limit|ferment-limit|com.ferment.dsp.limit"
  "Ferment Percept|ferment-percept|com.ferment.dsp.percept"
  "Ferment Master|ferment-master|com.ferment.dsp.master"
)

SUB_PKGS=()
CHOICE_LINES=()
CHOICE_DEFS=()
PKG_REFS=()

# Build per-plugin, per-format component pkgs
for entry in "${FERMENT_PLUGINS[@]}"; do
  IFS='|' read -r NAME SLUG BUNDLE_BASE <<< "$entry"
  ART="$BUILD_DIR/src-ferment/${SLUG}_artefacts/Release"

  # VST3
  if [ -d "$ART/VST3/$NAME.vst3" ]; then
    work="$TMPDIR/${SLUG}_vst3"
    mkdir -p "$work"
    cp -R "$ART/VST3/$NAME.vst3" "$work/"

    if [ -n "${MAC_SIGNING_CERT:-}" ]; then
      codesign --force -s "$MAC_SIGNING_CERT" -o runtime --deep "$work/$NAME.vst3"
    fi

    pkg="$TMPDIR/${SLUG}_VST3.pkg"
    if [ -n "${MAC_INSTALLING_CERT:-}" ]; then
      pkgbuild --sign "$MAC_INSTALLING_CERT" \
               --root "$work" --identifier "${BUNDLE_BASE}.vst3.pkg" \
               --version "$VERSION" --install-location "/Library/Audio/Plug-Ins/VST3" "$pkg"
    else
      pkgbuild --root "$work" --identifier "${BUNDLE_BASE}.vst3.pkg" \
               --version "$VERSION" --install-location "/Library/Audio/Plug-Ins/VST3" "$pkg"
    fi
    SUB_PKGS+=("$pkg")
    id="${BUNDLE_BASE}.vst3.pkg"
    CHOICE_LINES+=("<line choice=\"$id\"/>")
    CHOICE_DEFS+=("<choice id=\"$id\" visible=\"true\" start_selected=\"true\" title=\"$NAME (VST3)\"><pkg-ref id=\"$id\"/></choice><pkg-ref id=\"$id\" version=\"$VERSION\" onConclusion=\"none\">${SLUG}_VST3.pkg</pkg-ref>")
    PKG_REFS+=("<pkg-ref id=\"$id\"/>")
  fi

  # AU
  if [ -d "$ART/AU/$NAME.component" ]; then
    work="$TMPDIR/${SLUG}_au"
    mkdir -p "$work"
    cp -R "$ART/AU/$NAME.component" "$work/"

    if [ -n "${MAC_SIGNING_CERT:-}" ]; then
      codesign --force -s "$MAC_SIGNING_CERT" -o runtime --deep "$work/$NAME.component"
    fi

    pkg="$TMPDIR/${SLUG}_AU.pkg"
    if [ -n "${MAC_INSTALLING_CERT:-}" ]; then
      pkgbuild --sign "$MAC_INSTALLING_CERT" \
               --root "$work" --identifier "${BUNDLE_BASE}.component.pkg" \
               --version "$VERSION" --install-location "/Library/Audio/Plug-Ins/Components" "$pkg"
    else
      pkgbuild --root "$work" --identifier "${BUNDLE_BASE}.component.pkg" \
               --version "$VERSION" --install-location "/Library/Audio/Plug-Ins/Components" "$pkg"
    fi
    SUB_PKGS+=("$pkg")
    id="${BUNDLE_BASE}.component.pkg"
    CHOICE_LINES+=("<line choice=\"$id\"/>")
    CHOICE_DEFS+=("<choice id=\"$id\" visible=\"true\" start_selected=\"true\" title=\"$NAME (Audio Unit)\"><pkg-ref id=\"$id\"/></choice><pkg-ref id=\"$id\" version=\"$VERSION\" onConclusion=\"none\">${SLUG}_AU.pkg</pkg-ref>")
    PKG_REFS+=("<pkg-ref id=\"$id\"/>")
  fi
done

# Build distribution.xml
{
  echo '<?xml version="1.0" encoding="utf-8"?>'
  echo '<installer-gui-script minSpecVersion="1">'
  echo "  <title>Ferment Family $VERSION</title>"
  echo '  <license file="License.txt" />'
  echo '  <readme file="Readme.rtf" />'
  for r in "${PKG_REFS[@]}"; do echo "  $r"; done
  echo '  <options require-scripts="false" customize="always" hostArchitectures="x86_64,arm64" />'
  echo '  <choices-outline>'
  for c in "${CHOICE_LINES[@]}"; do echo "    $c"; done
  echo '  </choices-outline>'
  for d in "${CHOICE_DEFS[@]}"; do echo "  $d"; done
  echo '</installer-gui-script>'
} > "$TMPDIR/distribution.xml"

# Combine sub-pkgs into unified product pkg.
# Artifact naming: <name>-<version>-<os>-<arch>, all lowercase.
# arm64 on Apple Silicon builds, x86_64 on Intel; FERMENT_ARCH=universal
# overrides when CMAKE_OSX_ARCHITECTURES builds both.
ARCH="${FERMENT_ARCH:-$(uname -m)}"
OUT_BASE="ferment-family-$VERSION-macos-$ARCH"
pushd "$TMPDIR" > /dev/null

if [ -n "${MAC_INSTALLING_CERT:-}" ]; then
  productbuild --sign "$MAC_INSTALLING_CERT" --distribution distribution.xml \
               --package-path . --resources "$RESOURCES_DIR" "$OUT_BASE.pkg"
else
  productbuild --distribution distribution.xml --package-path . \
               --resources "$RESOURCES_DIR/" "$OUT_BASE.pkg"
fi

SetFile -a C "$OUT_BASE.pkg"
popd > /dev/null

# Wrap in DMG
DMGSRC="$TMPDIR/ferment-dmg-src"
rm -rf "$DMGSRC"
mkdir -p "$DMGSRC"
mv "$TMPDIR/$OUT_BASE.pkg" "$DMGSRC/"

rm -f "$OUTPUT_DIR/$OUT_BASE.dmg"
hdiutil create /tmp/ferment-tmp.dmg -ov -volname "$OUT_BASE" -fs HFS+ -srcfolder "$DMGSRC"
hdiutil convert /tmp/ferment-tmp.dmg -format UDZO -o "$OUTPUT_DIR/$OUT_BASE.dmg"
rm -f /tmp/ferment-tmp.dmg

if [ -n "${MAC_SIGNING_CERT:-}" ]; then
  codesign --force -s "$MAC_SIGNING_CERT" --timestamp "$OUTPUT_DIR/$OUT_BASE.dmg"
  codesign -vvv "$OUTPUT_DIR/$OUT_BASE.dmg"
  if [ -n "${MAC_SIGNING_ID:-}" ] && [ -n "${MAC_SIGNING_TEAM:-}" ] && [ -n "${MAC_SIGNING_1UPW:-}" ]; then
    xcrun notarytool submit "$OUTPUT_DIR/$OUT_BASE.dmg" \
      --apple-id "$MAC_SIGNING_ID" \
      --team-id  "$MAC_SIGNING_TEAM" \
      --password "$MAC_SIGNING_1UPW" --wait
    xcrun stapler staple "$OUTPUT_DIR/$OUT_BASE.dmg"
  fi
fi

echo "DMG created: $OUTPUT_DIR/$OUT_BASE.dmg"

#!/usr/bin/env bash
# ==============================================================================
#  Superalloy — macOS installer package (.pkg) for the universal VST3
#
#  Usage:  packaging/macos/build_pkg.sh <path/to/Superalloy.vst3> <output.pkg>
#
#  Wraps an already built, tested and signed Superalloy.vst3 (untouched, byte
#  for byte) in a standard macOS Installer package:
#
#    • installs to /Library/Audio/Plug-Ins/VST3/Superalloy.vst3 (all users;
#      the standard VST3 location every host scans; asks for an admin password)
#    • non-relocatable: the bundle always lands in that folder, even if a copy
#      with the same bundle ID exists elsewhere on the Mac (pkgbuild's default
#      would let Installer "upgrade" the stray copy in place instead)
#    • overwrites any existing Superalloy.vst3 in that folder, newer or older,
#      so reinstalling any version always gives exactly that version
#    • runs natively on Apple Silicon and Intel (no Rosetta prompt), macOS 11+
#    • no install scripts: the package only places the bundle
#
#  The package itself is unsigned. Signing it needs a "Developer ID Installer"
#  certificate (separate from "Developer ID Application"); without one, macOS
#  asks for confirmation the first time the downloaded .pkg is opened.
#
#  Runs on macOS only (pkgbuild, productbuild, PlistBuddy, lipo, codesign).
# ==============================================================================
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <path/to/Superalloy.vst3> <output.pkg>" >&2
    exit 2
fi

SRC="${1%/}"
OUT="$2"
HERE="$(cd "$(dirname "$0")" && pwd)"

PLIST_BUDDY=/usr/libexec/PlistBuddy
INSTALL_LOCATION="/Library/Audio/Plug-Ins/VST3"
COMPONENT_ID="com.wavrely.superalloy.vst3"
COMPONENT_PKG="Superalloy-vst3.pkg"
EXPECTED_BUNDLE_ID="com.wavrely.superalloy"

fail() { echo "build_pkg: $*" >&2; exit 1; }

# ------------------------------------------------------------------ the bundle
[ -d "$SRC" ] || fail "no bundle at $SRC"
[ "$(basename "$SRC")" = "Superalloy.vst3" ] || fail "expected Superalloy.vst3, got $(basename "$SRC")"
INFO="$SRC/Contents/Info.plist"
BIN="$SRC/Contents/MacOS/Superalloy"
[ -f "$INFO" ] || fail "missing $INFO"
[ -f "$BIN" ] || fail "missing $BIN"

BUNDLE_ID="$("$PLIST_BUDDY" -c "Print :CFBundleIdentifier" "$INFO")"
VERSION="$("$PLIST_BUDDY" -c "Print :CFBundleShortVersionString" "$INFO")"
[ "$BUNDLE_ID" = "$EXPECTED_BUNDLE_ID" ] || fail "bundle ID is $BUNDLE_ID, expected $EXPECTED_BUNDLE_ID"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "unexpected version string '$VERSION'"

lipo "$BIN" -verify_arch arm64 x86_64 || fail "binary is not universal (arm64 + x86_64): $(lipo -archs "$BIN")"
codesign --verify --deep --strict "$SRC" || fail "bundle signature does not verify"

echo "Bundle:    $SRC"
echo "ID:        $BUNDLE_ID"
echo "Version:   $VERSION"
echo "Slices:    $(lipo -archs "$BIN")"
echo "Signature: $(codesign -dv "$SRC" 2>&1 | grep -E '^(Signature|Authority)=' | head -1)"

# ------------------------------------------------------------------ staging
WORK="$(mktemp -d "${TMPDIR:-/tmp}/superalloy-pkg.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
ROOT="$WORK/root"
COMPONENTS="$WORK/components"
RESOURCES="$WORK/resources"
mkdir -p "$ROOT" "$COMPONENTS" "$RESOURCES"

# ditto keeps the bundle exactly as signed; --noqtn drops any quarantine flag
# the copy may have picked up, so it never reaches the payload.
ditto --noqtn "$SRC" "$ROOT/Superalloy.vst3"

# The staged copy must be byte-identical to the source.
( cd "$SRC"  && find . -type f | LC_ALL=C sort | while IFS= read -r f; do shasum -a 256 "$f"; done ) > "$WORK/src.sha"
( cd "$ROOT/Superalloy.vst3" && find . -type f | LC_ALL=C sort | while IFS= read -r f; do shasum -a 256 "$f"; done ) > "$WORK/stage.sha"
cmp -s "$WORK/src.sha" "$WORK/stage.sha" || fail "staged bundle differs from the source bundle"

# ------------------------------------------------------------------ component
COMPONENT_PLIST="$WORK/component.plist"
pkgbuild --analyze --root "$ROOT" "$COMPONENT_PLIST" > /dev/null
"$PLIST_BUDDY" -c "Print :0:RootRelativeBundlePath" "$COMPONENT_PLIST" | grep -qx "Superalloy.vst3" \
    || fail "component plist does not describe Superalloy.vst3"
if "$PLIST_BUDDY" -c "Print :1" "$COMPONENT_PLIST" > /dev/null 2>&1; then
    fail "component plist lists more than one bundle"
fi
"$PLIST_BUDDY" -c "Set :0:BundleIsRelocatable false"        "$COMPONENT_PLIST"
"$PLIST_BUDDY" -c "Set :0:BundleIsVersionChecked false"     "$COMPONENT_PLIST"
"$PLIST_BUDDY" -c "Set :0:BundleHasStrictIdentifier true"   "$COMPONENT_PLIST"
"$PLIST_BUDDY" -c "Set :0:BundleOverwriteAction upgrade"    "$COMPONENT_PLIST"

pkgbuild \
    --root "$ROOT" \
    --component-plist "$COMPONENT_PLIST" \
    --identifier "$COMPONENT_ID" \
    --version "$VERSION" \
    --install-location "$INSTALL_LOCATION" \
    "$COMPONENTS/$COMPONENT_PKG"

# ------------------------------------------------------------------ installer pages
mkdir -p "$RESOURCES/en.lproj"
for page in welcome.html conclusion.html; do
    [ -f "$HERE/resources/$page" ] || fail "missing $HERE/resources/$page"
    sed "s/@VERSION@/$VERSION/g" "$HERE/resources/$page" > "$RESOURCES/en.lproj/$page"
done

# ------------------------------------------------------------------ distribution
cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Superalloy $VERSION</title>
    <organization>com.wavrely</organization>
    <welcome file="welcome.html" mime-type="text/html"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <volume-check>
        <allowed-os-versions>
            <os-version min="11.0"/>
        </allowed-os-versions>
    </volume-check>
    <choices-outline>
        <line choice="$COMPONENT_ID"/>
    </choices-outline>
    <choice id="$COMPONENT_ID" title="Superalloy VST3" visible="false">
        <pkg-ref id="$COMPONENT_ID"/>
    </choice>
    <pkg-ref id="$COMPONENT_ID" version="$VERSION" onConclusion="none">$COMPONENT_PKG</pkg-ref>
</installer-gui-script>
EOF

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
productbuild \
    --distribution "$WORK/distribution.xml" \
    --resources "$RESOURCES" \
    --package-path "$COMPONENTS" \
    "$OUT"

echo
echo "Package:   $OUT"
echo "Installs:  $INSTALL_LOCATION/Superalloy.vst3"
echo "Size:      $(stat -f %z "$OUT") bytes"
echo "SHA-256:   $(shasum -a 256 "$OUT" | cut -d' ' -f1)"

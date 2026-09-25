#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════
#  Latex Compiler — DMG packaging (macOS)
#
#  Builds the release bundle, verifies its signature and packs a
#  compressed, distributable .dmg with a drag-to-Applications layout.
#
#  Output: dist/Latex Compiler-<version>-<arch>.dmg
#
#  Note: the bundle is ad-hoc signed. On other machines Gatekeeper will
#  ask to confirm the app on first open (right-click → Open). Notarized
#  Developer ID signing is the step to add before public distribution.
#
#  Usage: scripts/package_dmg.sh [--skip-build]
# ═══════════════════════════════════════════════════════════════════
set -euo pipefail

cd "$(dirname "$0")/.."

SKIP_BUILD=0
for arg in "$@"; do
  case "$arg" in
    --skip-build) SKIP_BUILD=1 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

APP_NAME="Latex Compiler"
BUNDLE_ID="com.Misaint20.latexcompiler"
BUILD_DIR="build/release"
APP_PATH="${BUILD_DIR}/${APP_NAME}.app"
STAGING="$(mktemp -d)"
trap 'rm -rf "$STAGING"' EXIT

if [[ "$SKIP_BUILD" -eq 0 ]]; then
  echo "==> Building release bundle"
  cmake --preset release
  cmake --build --preset release
fi

if [[ ! -d "$APP_PATH" ]]; then
  echo "error: bundle not found at $APP_PATH (build first or drop --skip-build)" >&2
  exit 1
fi

# The release preset shares the project's macOS signing target, but verify
# instead of trusting: an unsealed bundle would install badly elsewhere.
echo "==> Verifying signature of ${APP_NAME}.app"
codesign --verify --strict "$APP_PATH"
ACTUAL_ID="$(codesign -dv "$APP_PATH" 2>&1 | sed -n 's/^Identifier=//p' | head -1)"
PLIST_ID="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$APP_PATH/Contents/Info.plist")"
if [[ "$ACTUAL_ID" != "$BUNDLE_ID" || "$PLIST_ID" != "$BUNDLE_ID" ]]; then
  echo "error: bundle identity mismatch (signature: '$ACTUAL_ID', plist: '$PLIST_ID', expected: '$BUNDLE_ID')" >&2
  exit 1
fi

VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP_PATH/Contents/Info.plist")"
ARCH="$(uname -m)"
DMG_NAME="${APP_NAME}-${VERSION}-${ARCH}.dmg"
DMG_PATH="dist/${DMG_NAME}"
mkdir -p dist
rm -f "$DMG_PATH" "${DMG_PATH%.dmg}.tmp.dmg" "${DMG_PATH%.dmg}.dmg.tmp.dmg"

echo "==> Staging ${APP_NAME}.app ${VERSION} (${ARCH})"
cp -R "$APP_PATH" "$STAGING/"
ln -s /Applications "$STAGING/Applications"

echo "==> Building $DMG_NAME"
# hdiutil appends .dmg when the output name lacks it, so the temp file must
# already end in .dmg for the atomic rename below to find it.
hdiutil create -volname "$APP_NAME $VERSION" \
  -srcfolder "$STAGING" \
  -ov -format UDZO \
  "${DMG_PATH%.dmg}.tmp.dmg" >/dev/null

mv "${DMG_PATH%.dmg}.tmp.dmg" "$DMG_PATH"

echo "==> Verifying the image"
hdiutil verify "$DMG_PATH" >/dev/null
SIZE="$(du -h "$DMG_PATH" | cut -f1)"

echo ""
echo "✓ $DMG_PATH ($SIZE)"
echo "  Install: open the DMG and drag ${APP_NAME} to Applications."

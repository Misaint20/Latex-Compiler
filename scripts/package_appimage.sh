#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════
#  Latex Compiler — AppImage packaging (Linux, run ON Linux)
#
#  Builds the release binary, installs it into an AppDir with the
#  .desktop entry and icon, and produces an AppImage via linuxdeploy
#  (which supplies the AppRun runtime and rewrites Exec/Icon paths).
#
#  Output: dist/Latex Compiler-<version>-<arch>.AppImage
#
#  Portability note: the AppImage bundles the webview/webkit-gtk shared
#  libraries linuxdeploy finds outside the system whitelist. It runs on
#  distros with a glibc >= the build machine's — for wide coverage, build
#  on the oldest distro you intend to support (or in a manylinux image).
#
#  Usage: scripts/package_appimage.sh [--skip-build]
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
BUILD_DIR="build/release"
APPDIR="$(mktemp -d)/AppDir"
# grep -E is portable (BSD and GNU); -P and sed \( groups are not.
VERSION="$(grep -oE 'project\(LatexCompiler VERSION [0-9.]+' CMakeLists.txt | head -1 | grep -oE '[0-9.]+')"
if [[ -z "$VERSION" ]]; then
  echo "error: could not read the project version from CMakeLists.txt" >&2
  exit 1
fi
ARCH="$(uname -m)"
DIST="dist"
mkdir -p "$DIST"
rm -f "$DIST/${APP_NAME}-${VERSION}-${ARCH}.AppImage" "${APP_NAME}-${VERSION}-${ARCH}.AppImage.tmp"

if [[ "$SKIP_BUILD" -eq 0 ]]; then
  echo "==> Building release"
  cmake --preset release
  cmake --build --preset release
fi

echo "==> Installing into AppDir (provides bin/ + share/applications + icon)"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" \
  --prefix /usr
# DESTDIR=$APPDIR --prefix /usr yields: AppDir/usr/bin/, AppDir/usr/share/...
# linuxdeploy expects exactly that layout.

echo "==> Checking dependencies"
# AppRun runtime + linuxdeploy tool itself (single-file AppImages).
TOOLS_DIR="build/appimage-tools"
mkdir -p "$TOOLS_DIR"
if [[ ! -x "$TOOLS_DIR/linuxdeploy" ]]; then
  # -f: a 404 must fail the step, not save an HTML error page as the tool.
  curl -fsSL "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage" \
    -o "$TOOLS_DIR/linuxdeploy"
  chmod +x "$TOOLS_DIR/linuxdeploy"
fi
if [[ ! -e "$TOOLS_DIR/runtime-${ARCH}" ]]; then
  curl -fsSL "https://github.com/AppImage/AppImageKit/releases/download/continuous/runtime-${ARCH}" \
    -o "$TOOLS_DIR/runtime-${ARCH}"
  chmod +x "$TOOLS_DIR/runtime-${ARCH}"
fi

echo "==> Deploying (bundling libraries, wiring desktop entry + icon)"
export OUTPUT="$DIST/${APP_NAME}-${VERSION}-${ARCH}.AppImage"
"$TOOLS_DIR/linuxdeploy" \
  --appdir "$APPDIR" \
  --desktop-file "$APPDIR/usr/share/applications/latexcompiler.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/512x512/apps/latexcompiler.png" \
  --output appimage

# ---- GPG signature (integrity + authenticity for downloads) ----
# Set GPG_KEY to a fingerprint or email of a key in your keyring. The detached
# ASCII signature lands next to the artifact (.sig) together with an export of
# the public key, so verifiers need nothing else. gpg-agent prompts for the
# passphrase locally; for CI use a passphrase-less key or loopback pinentry.
if [[ -z "${GPG_KEY:-}" ]]; then
  echo "==> GPG signature skipped (set GPG_KEY=<fingerprint|email> to sign)"
else
  if ! command -v gpg >/dev/null 2>&1; then
    echo "warning: gpg not found; shipping UNSIGNED (install gnupg)" >&2
  else
    echo "==> Signing with GPG (${GPG_KEY})"
    gpg --local-user "$GPG_KEY" --detach-sign --armor \
      --output "$OUTPUT.sig" "$OUTPUT"
    gpg --armor --export "$GPG_KEY" > "$DIST/latexcompiler-release-key.asc"
    # Prove the pair verifies before shipping it.
    gpg --verify "$OUTPUT.sig" "$OUTPUT"
    echo "   signature:  $OUTPUT.sig"
    echo "   public key: $DIST/latexcompiler-release-key.asc (publish: keyserver / GitHub profile)"
  fi
fi

echo "==> Verifying"
if [[ -x "$OUTPUT" ]]; then
  SIZE="$(du -h "$OUTPUT" | cut -f1)"
  echo ""
  echo "✓ $OUTPUT ($SIZE)"
  echo "  Run: chmod +x '$OUTPUT' && '$OUTPUT'"
  echo "  Menu integration: copy the bundled .desktop to ~/.local/share/applications/"
  echo "    or use AppImageLauncher for automatic registration."
else
  echo "error: AppImage was not produced; check linuxdeploy output above" >&2
  exit 1
fi

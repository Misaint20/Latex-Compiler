#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════
#  Latex Compiler — Linux portability validator (run from any host)
#
#  Reproduces the CI Linux job locally in Docker: same base image
#  (Ubuntu 24.04), same dependency set, same build + test commands.
#  Catches gcc/libstdc++ portability errors (missing standard includes,
#  POSIX-only code paths) before pushing, without burning Actions
#  minutes.
#
#  Your working tree is mounted READ-ONLY; the container validates a
#  throwaway copy, so build outputs never touch the checkout. Host
#  build/ and node_modules/ (macOS binaries) are excluded from the
#  copy on purpose: a clean, fully Linux build is the point.
#
#  Usage:
#    scripts/validate-linux.sh              # build + full test suite
#    scripts/validate-linux.sh --appimage   # also package the AppImage
#    scripts/validate-linux.sh --shell      # interactive shell with the
#                                           # dependencies preinstalled
#
#  Requires Docker running locally. The first run downloads ~600 MB
#  (image + packages); later runs reuse the cached image.
# ═══════════════════════════════════════════════════════════════════
set -euo pipefail

cd "$(dirname "$0")/.."

MODE="build"
for arg in "$@"; do
  case "$arg" in
    --appimage) MODE="appimage" ;;
    --shell) MODE="shell" ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

if ! command -v docker >/dev/null 2>&1; then
  echo "error: docker not found. Install Docker Desktop (or colima) first." >&2
  exit 1
fi
if ! docker info >/dev/null 2>&1; then
  echo "error: the Docker daemon is not running. Start Docker Desktop and retry." >&2
  exit 1
fi

echo "==> Building the validation image (cached after the first run)"
# No context needed (the image installs nothing from the repo), so the
# Dockerfile travels on stdin. Same packages as the CI workflow's
# "Install Linux dependencies" step, plus node 22 + pnpm 11.15.1 to
# match actions/setup-node and the packageManager pin.
docker build -q -t latex-compiler-linux-validate - <<'DOCKERFILE'
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl git gnupg pkg-config ninja-build tar \
      libwebkit2gtk-4.1-dev libgtk-3-dev \
 && (apt-get install -y --no-install-recommends libfuse2 \
      || apt-get install -y --no-install-recommends libfuse2t64) \
 && rm -rf /var/lib/apt/lists/*
RUN curl -fsSL https://deb.nodesource.com/setup_22.x | bash - \
 && apt-get install -y --no-install-recommends nodejs \
 && npm install -g pnpm@11.15.1 \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
DOCKERFILE

# Read-only mount; --tty only for the interactive shell (a TTY corrupts
# piped scripts).
MOUNTS=(--rm --interactive --volume "$PWD:/src:ro" --workdir /src)

case "$MODE" in
  shell)
    echo "==> Interactive validator shell (repo mounted read-only; copy it to /work first)"
    exec docker run "${MOUNTS[@]}" --tty latex-compiler-linux-validate bash
    ;;
  build|appimage)
    echo "==> Validating Linux build + tests (mode: $MODE)"
    docker run "${MOUNTS[@]}" latex-compiler-linux-validate bash -s "$MODE" <<'INSIDE'
set -euo pipefail

# Copy the tree out of the read-only mount, excluding host-platform
# artifacts so nothing macOS-built can mask a Linux problem.
echo "==> Copying sources into a scratch work tree (host artifacts excluded)"
mkdir -p /work
tar -C /src -cf - \
    --exclude=./.git \
    --exclude=./build \
    --exclude=./dist \
    --exclude=./frontend/node_modules \
    --exclude=./resources/frontend \
    . | tar -C /work -xf -
cd /work

echo "==> cmake configure (release)"
cmake --preset release

echo "==> Building (the real portability test)"
cmake --build --preset release

echo "==> Running the full test suite"
(cd build/release && ctest --output-on-failure)

if [ "$1" = "appimage" ]; then
  echo "==> Packaging AppImage (extract-and-run: the container has no FUSE)"
  APPIMAGE_EXTRACT_AND_RUN=1 ./scripts/package_appimage.sh --skip-build
  echo "==> AppImage produced:"
  for image in dist/*.AppImage; do
    echo "    $image"
  done
fi

echo ""
echo "✓ Linux validation passed"
INSIDE
    ;;
esac

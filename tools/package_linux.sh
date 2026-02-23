#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-}"
BUILD_DIR="${2:-build-release-linux}"

if [[ -z "$VERSION" ]]; then
  echo "Usage: $0 <version> [build_dir]"
  echo "Example: $0 1.1.0"
  exit 1
fi

cmake -S . -B "$BUILD_DIR" -G Ninja -DCHESS_RELEASE_VERSION="$VERSION"
cmake --build "$BUILD_DIR" --target chess_linux_release

ARCH="$(uname -m)"
case "$ARCH" in
  x86_64|amd64) ARCH_LABEL="x64" ;;
  aarch64|arm64) ARCH_LABEL="arm64" ;;
  *) ARCH_LABEL="$ARCH" ;;
esac

echo
echo "Linux release package created:"
echo "  $BUILD_DIR/release/chess-linux-${ARCH_LABEL}-v${VERSION}.run"
echo "  $BUILD_DIR/release/chess-linux-${ARCH_LABEL}-v${VERSION}.run.sha256"
echo "  $BUILD_DIR/release/chess-linux-${ARCH_LABEL}-v${VERSION}.tar.gz"
echo "  $BUILD_DIR/release/chess-linux-${ARCH_LABEL}-v${VERSION}.tar.gz.sha256"

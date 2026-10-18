#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-}"
BUILD_DIR="${2:-build-release-linux}"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
if [[ "$BUILD_DIR" != /* ]]; then
  BUILD_DIR="$SOURCE_DIR/$BUILD_DIR"
fi

if [[ -z "$VERSION" ]]; then
  echo "Usage: $0 <version> [build_dir]"
  echo "Example: $0 1.1.0"
  exit 1
fi

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCHESS_RELEASE_VERSION="$VERSION"
cmake --build "$BUILD_DIR"
ctest --test-dir "$BUILD_DIR" --output-on-failure
cmake --build "$BUILD_DIR" --target chess_linux_release

echo
echo "Linux release packages created in: $BUILD_DIR/release"

#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

DO_LINUX=true
DO_WINDOWS=true
DO_UPLOAD=true
TAG_NAME=""
WINDOWS_MODE="static"
MXE_QT="${MXE_QT:-qt5}"
MXE_ROOT="${MXE_ROOT:-/opt/mxe}"

usage() {
    cat <<EOF
Usage: $0 [--tag <tag>] [--no-upload] [--no-linux] [--no-windows] [--shared|--static] [--qt5|--qt6]

Examples:
  ./setup.sh --tag v4.16.3
  MXE_QT=qt6 ./setup.sh --shared
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --tag)
            TAG_NAME="${2:-}"
            shift
            ;;
        --no-upload)
            DO_UPLOAD=false
            ;;
        --no-linux)
            DO_LINUX=false
            ;;
        --no-windows)
            DO_WINDOWS=false
            ;;
        --shared)
            WINDOWS_MODE="shared"
            ;;
        --static)
            WINDOWS_MODE="static"
            ;;
        --qt5)
            MXE_QT="qt5"
            ;;
        --qt6)
            MXE_QT="qt6"
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage
            exit 1
            ;;
    esac
    shift
done

VERSION="$(awk -F'=' '/^VERSION[[:space:]]*=/{gsub(/ /,"",$2); print $2; exit}' src/WhatsApp.pro || true)"
BUILD_NUM="$(tr -d '[:space:]' < src/BUILD_NUMBER 2>/dev/null || true)"
VERSION="${VERSION:-0.0.0}"
BUILD_NUM="${BUILD_NUM:-0}"

if [[ -z "$TAG_NAME" ]]; then
    TAG_NAME="v${VERSION}.${BUILD_NUM}"
fi

if [[ "$DO_LINUX" = true ]]; then
    ./build.sh
    ./package_linux.sh --all --no-build
fi

if [[ "$DO_WINDOWS" = true ]]; then
    export MXE_ROOT
    export MXE_QT
    if [[ "$WINDOWS_MODE" = "shared" ]]; then
        ./build_windows_mxe.sh --shared
        ./package_windows_mxe.sh --shared
    else
        ./build_windows_mxe.sh --static
        ./package_windows_mxe.sh --static
    fi
fi

if [[ "$DO_UPLOAD" = true ]]; then
    if ! command -v gh >/dev/null 2>&1; then
        echo "ERROR: gh CLI not found. Install gh or pass --no-upload." >&2
        exit 1
    fi
    if ! gh auth status >/dev/null 2>&1; then
        echo "ERROR: gh is not authenticated. Run: gh auth login" >&2
        exit 1
    fi

    if ! gh release view "$TAG_NAME" >/dev/null 2>&1; then
        gh release create "$TAG_NAME" --title "$TAG_NAME" --notes "Automated release"
    fi

    shopt -s nullglob
    gh release upload "$TAG_NAME" build/package/out/*.deb build/package/out/*.AppImage build/package/out/*.exe --clobber
    shopt -u nullglob
fi

echo "Done. Artifacts in build/package/out/"

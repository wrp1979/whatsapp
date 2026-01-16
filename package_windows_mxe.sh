#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

MXE_ROOT="${MXE_ROOT:-/opt/mxe}"
MXE_QT="${MXE_QT:-qt5}"
MXE_TARGET="${MXE_TARGET:-x86_64-w64-mingw32.static}"
WINDEPLOYQT_BIN="${WINDEPLOYQT_BIN:-}"
MAKE_NSIS_BIN="${MAKE_NSIS_BIN:-makensis}"
SKIP_DEPLOY=false

usage() {
    cat <<EOF
Usage: $0 [--static|--shared] [--qt5|--qt6] [--skip-deploy]

Environment:
  MXE_ROOT        (default: /opt/mxe)
  MXE_QT          (default: qt5)
  MXE_TARGET      (default: x86_64-w64-mingw32.static)
  WINDEPLOYQT_BIN (override windeployqt path)
  MAKE_NSIS_BIN   (override makensis path)
  WINDEPLOYQT_ARGS (extra args for windeployqt)
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --static)
            MXE_TARGET="x86_64-w64-mingw32.static"
            ;;
        --shared)
            MXE_TARGET="x86_64-w64-mingw32.shared"
            ;;
        --qt5)
            MXE_QT="qt5"
            ;;
        --qt6)
            MXE_QT="qt6"
            ;;
        --skip-deploy)
            SKIP_DEPLOY=true
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

BUILD_DIR="$ROOT_DIR/build/windows/mxe/$MXE_TARGET"
DEPLOY_DIR="$BUILD_DIR/deploy"
OUT_DIR="$ROOT_DIR/build/package/out"
NSIS_SCRIPT="$ROOT_DIR/packaging/windows/whatsie.nsi"

EXE_PATH="$BUILD_DIR/whatsie.exe"
if [[ ! -f "$EXE_PATH" ]]; then
    EXE_PATH="$(find "$BUILD_DIR" -name "whatsie.exe" -type f -maxdepth 3 2>/dev/null | head -1 || true)"
fi
if [[ -z "$EXE_PATH" || ! -f "$EXE_PATH" ]]; then
    echo "ERROR: whatsie.exe not found under $BUILD_DIR. Run ./build_windows_mxe.sh first." >&2
    exit 1
fi

if [[ -z "$WINDEPLOYQT_BIN" ]]; then
    WINDEPLOYQT_BIN="$MXE_ROOT/usr/$MXE_TARGET/$MXE_QT/bin/windeployqt"
fi

mkdir -p "$DEPLOY_DIR"
mkdir -p "$OUT_DIR"

if [[ "$SKIP_DEPLOY" = false ]]; then
    rm -rf "$DEPLOY_DIR"
    mkdir -p "$DEPLOY_DIR"
    cp "$EXE_PATH" "$DEPLOY_DIR/whatsie.exe"

    if [[ -x "$WINDEPLOYQT_BIN" ]]; then
        "$WINDEPLOYQT_BIN" \
            --release \
            --no-translations \
            --compiler-runtime \
            ${WINDEPLOYQT_ARGS:-} \
            "$DEPLOY_DIR/whatsie.exe"
    else
        echo "ERROR: windeployqt not found: $WINDEPLOYQT_BIN" >&2
        exit 1
    fi
else
    rm -rf "$DEPLOY_DIR"
    mkdir -p "$DEPLOY_DIR"
    cp "$EXE_PATH" "$DEPLOY_DIR/whatsie.exe"
fi

VERSION="$(awk -F'=' '/^VERSION[[:space:]]*=/{gsub(/ /,"",$2); print $2; exit}' src/WhatsApp.pro || true)"
BUILD_NUM="$(tr -d '[:space:]' < src/BUILD_NUMBER 2>/dev/null || true)"
VERSION="${VERSION:-0.0.0}"
BUILD_NUM="${BUILD_NUM:-0}"

IFS='.' read -r v1 v2 v3 <<< "$VERSION"
v1="${v1:-0}"
v2="${v2:-0}"
v3="${v3:-0}"
NSIS_VERSION="${v1}.${v2}.${v3}.${BUILD_NUM}"

OUT_FILE="$OUT_DIR/WhatSie-${VERSION}.${BUILD_NUM}-setup.exe"

if ! command -v "$MAKE_NSIS_BIN" >/dev/null 2>&1; then
    echo "ERROR: NSIS not found (makensis)." >&2
    exit 1
fi

"$MAKE_NSIS_BIN" \
    /DPRODUCT_NAME=WhatSie \
    /DPRODUCT_EXE=whatsie.exe \
    /DPRODUCT_VERSION="$NSIS_VERSION" \
    /DPRODUCT_PUBLISHER=WhatSie \
    /DSOURCE_DIR="$DEPLOY_DIR" \
    /DOUT_FILE="$OUT_FILE" \
    "$NSIS_SCRIPT"

echo "Created: $OUT_FILE"

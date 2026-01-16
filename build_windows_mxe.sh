#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

MXE_ROOT="${MXE_ROOT:-/opt/mxe}"
MXE_QT="${MXE_QT:-qt5}"
MXE_TARGET="${MXE_TARGET:-x86_64-w64-mingw32.static}"
QMAKE_BIN="${QMAKE_BIN:-}"
MAKE_JOBS="${MAKE_JOBS:-$(nproc)}"
DO_CLEAN=false

usage() {
    cat <<EOF
Usage: $0 [--static|--shared] [--qt5|--qt6] [--clean]

Environment:
  MXE_ROOT   (default: /opt/mxe)
  MXE_QT     (default: qt5)
  MXE_TARGET (default: x86_64-w64-mingw32.static)
  QMAKE_BIN  (override qmake path)
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
        --clean)
            DO_CLEAN=true
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

export PATH="$MXE_ROOT/usr/bin:/usr/bin:/bin:$PATH"

if [[ -z "$QMAKE_BIN" ]]; then
    QMAKE_BIN="$MXE_ROOT/usr/$MXE_TARGET/$MXE_QT/bin/qmake"
fi

if [[ ! -x "$QMAKE_BIN" ]]; then
    echo "ERROR: QMake not found: $QMAKE_BIN" >&2
    exit 1
fi

QT_HEADERS_DIR="$("$QMAKE_BIN" -query QT_INSTALL_HEADERS 2>/dev/null || true)"
if [[ -z "$QT_HEADERS_DIR" ]]; then
    QT_HEADERS_DIR="$MXE_ROOT/usr/$MXE_TARGET/$MXE_QT/include"
fi

WEBENGINE_HEADERS="$QT_HEADERS_DIR/QtWebEngineWidgets"
if [[ ! -d "$WEBENGINE_HEADERS" ]]; then
    echo "ERROR: Qt WebEngine headers not found: $WEBENGINE_HEADERS" >&2
    echo "Build Qt WebEngine in MXE for this target before continuing." >&2
    exit 1
fi

BUILD_DIR="$ROOT_DIR/build/windows/mxe/$MXE_TARGET"

if [[ "$DO_CLEAN" = true ]]; then
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

"$QMAKE_BIN" "$ROOT_DIR/src/WhatsApp.pro" CONFIG+=release
make -j"$MAKE_JOBS"

if [[ ! -f "$BUILD_DIR/whatsie.exe" ]]; then
    echo "WARNING: whatsie.exe not found in $BUILD_DIR. Searching..." >&2
    find "$BUILD_DIR" -name "whatsie.exe" -type f -maxdepth 3 -print -quit
fi

#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

APP_NAME="whatsie"
APP_ID="com.ktechpit.whatsie"
APP_DISPLAY_NAME="WhatSie"

BUILD_DIR="$ROOT_DIR/build"
PACKAGE_DIR="$BUILD_DIR/package"
STAGE_DIR="$PACKAGE_DIR/stage"
OUT_DIR="$PACKAGE_DIR/out"

DO_DEB=false
DO_APPIMAGE=false
DO_UPLOAD_GITHUB=false
NO_BUILD=false
TAG_NAME=""

LINUXDEPLOY_DEFAULT="$HOME/bin/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT_DEFAULT="$HOME/bin/linuxdeploy-plugin-qt-x86_64.AppImage"
APPIMAGETOOL_DEFAULT="$HOME/bin/appimagetool-x86_64.AppImage"

log() { printf '%s\n' "$*"; }
fail() { echo "ERROR: $*" >&2; exit 1; }

usage() {
    cat <<EOF
Usage: $0 [--deb] [--appimage] [--all] [--upload-github] [--tag <tag>] [--no-build]

Options:
  --deb            Generate .deb package
  --appimage       Generate AppImage
  --all            Generate all formats (default)
  --upload-github  Upload artifacts to GitHub release via gh
  --tag <tag>      GitHub release tag (default: v<version>.<build>)
  --no-build       Do not invoke ./build.sh automatically
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --deb)
            DO_DEB=true
            ;;
        --appimage)
            DO_APPIMAGE=true
            ;;
        --all)
            DO_DEB=true
            DO_APPIMAGE=true
            ;;
        --upload-github)
            DO_UPLOAD_GITHUB=true
            ;;
        --tag)
            TAG_NAME="${2:-}"
            shift
            ;;
        --no-build)
            NO_BUILD=true
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

if [[ "$DO_DEB" = false && "$DO_APPIMAGE" = false ]]; then
    DO_DEB=true
    DO_APPIMAGE=true
fi

VERSION="$(awk -F'=' '/^VERSION[[:space:]]*=/{gsub(/ /,"",$2); print $2; exit}' src/WhatsApp.pro || true)"
BUILD_NUM="$(tr -d '[:space:]' < src/BUILD_NUMBER 2>/dev/null || true)"
VERSION="${VERSION:-0.0.0}"
BUILD_NUM="${BUILD_NUM:-0}"

PKG_VERSION="${VERSION}.${BUILD_NUM}"
DEB_VERSION="${VERSION}-${BUILD_NUM}"
ARCH="$(uname -m)"
DEB_ARCH="$(dpkg --print-architecture 2>/dev/null || echo amd64)"

LINUXDEPLOY="${LINUXDEPLOY:-$LINUXDEPLOY_DEFAULT}"
LINUXDEPLOY_QT="${LINUXDEPLOY_QT:-$LINUXDEPLOY_QT_DEFAULT}"
APPIMAGETOOL="${APPIMAGETOOL:-$APPIMAGETOOL_DEFAULT}"

ensure_build() {
    if [[ ! -x "$BUILD_DIR/whatsie" ]]; then
        if [[ "$NO_BUILD" = true ]]; then
            fail "Binary not found at $BUILD_DIR/whatsie. Run ./build.sh first."
        fi
        log "Binary not found. Running ./build.sh..."
        ./build.sh
    fi
    if [[ ! -f "$BUILD_DIR/Makefile" ]]; then
        if [[ "$NO_BUILD" = true ]]; then
            fail "Build Makefile not found. Run ./build.sh first."
        fi
        log "Build Makefile not found. Running ./build.sh..."
        ./build.sh
    fi
}

stage_install() {
    rm -rf "$STAGE_DIR"
    mkdir -p "$STAGE_DIR"
    make -C "$BUILD_DIR" install INSTALL_ROOT="$STAGE_DIR"
    if [[ ! -x "$STAGE_DIR/usr/bin/whatsie" ]]; then
        fail "Install staging failed: $STAGE_DIR/usr/bin/whatsie not found."
    fi
}

compute_deps() {
    if ! command -v ldd >/dev/null 2>&1; then
        echo ""
        return
    fi
    if ! command -v dpkg >/dev/null 2>&1; then
        echo ""
        return
    fi
    local libs
    libs="$(ldd "$BUILD_DIR/whatsie" 2>/dev/null | grep -oE '/(lib|usr/lib)[^ ]+' | sort -u || true)"
    if [[ -z "$libs" ]]; then
        echo ""
        return
    fi
    local pkgs
    pkgs="$(printf '%s\n' "$libs" | xargs -r -I{} dpkg -S {} 2>/dev/null \
        | grep -v '^diversion' \
        | awk -F: 'NF>1 {print $1}' \
        | grep -E '^[a-z0-9][a-z0-9+.-]*$' \
        | sort -u | tr '\n' ',' | sed 's/,$//' | sed 's/,/, /g')"
    echo "$pkgs"
}

generate_deb() {
    command -v dpkg-deb >/dev/null 2>&1 || fail "dpkg-deb not found."

    mkdir -p "$OUT_DIR"
    local pkg_dir="$OUT_DIR/deb/${APP_NAME}_${DEB_VERSION}_${DEB_ARCH}"
    rm -rf "$pkg_dir"
    mkdir -p "$pkg_dir/DEBIAN"
    cp -a "$STAGE_DIR/usr" "$pkg_dir/"

    local deps
    deps="$(compute_deps)"
    deps="${deps:-libc6}"

    cat > "$pkg_dir/DEBIAN/control" <<EOF
Package: ${APP_NAME}
Version: ${DEB_VERSION}
Section: net
Priority: optional
Architecture: ${DEB_ARCH}
Maintainer: ${APP_DISPLAY_NAME} contributors
Depends: ${deps}
Homepage: https://github.com/keshavbhatt/whatsie
Description: ${APP_DISPLAY_NAME} - WhatsApp Web client based on Qt WebEngine
 A feature rich WhatsApp Web client for Linux.
EOF

    cat > "$pkg_dir/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
update-desktop-database -q 2>/dev/null || true
gtk-update-icon-cache -q /usr/share/icons/hicolor 2>/dev/null || true
EOF
    chmod 755 "$pkg_dir/DEBIAN/postinst"

    local deb_file="$OUT_DIR/${APP_NAME}_${DEB_VERSION}_${DEB_ARCH}.deb"
    dpkg-deb --build "$pkg_dir" "$deb_file"
    log "Created: $deb_file"
}

prepare_linuxdeploy_tools() {
    local tools_dir="$PACKAGE_DIR/tools"
    mkdir -p "$tools_dir"

    [[ -f "$LINUXDEPLOY" ]] || fail "linuxdeploy not found at $LINUXDEPLOY"
    ln -sf "$LINUXDEPLOY" "$tools_dir/linuxdeploy"
    chmod +x "$tools_dir/linuxdeploy"

    [[ -f "$LINUXDEPLOY_QT" ]] || fail "linuxdeploy-plugin-qt not found at $LINUXDEPLOY_QT"
    ln -sf "$LINUXDEPLOY_QT" "$tools_dir/linuxdeploy-plugin-qt"
    chmod +x "$tools_dir/linuxdeploy-plugin-qt"

    export PATH="$tools_dir:$PATH"
    if [[ -z "${QMAKE:-}" ]]; then
        export QMAKE
        QMAKE="$(command -v qmake6 || command -v qmake || true)"
    fi
}

generate_appimage() {
    mkdir -p "$OUT_DIR"
    local appdir="$PACKAGE_DIR/AppDir"
    rm -rf "$appdir"
    mkdir -p "$appdir"
    cp -a "$STAGE_DIR/usr" "$appdir/"

    local desktop_file="$appdir/usr/share/applications/${APP_ID}.desktop"
    local icon_file="$appdir/usr/share/icons/hicolor/256x256/apps/${APP_ID}.png"
    if [[ ! -f "$desktop_file" ]]; then
        mkdir -p "$(dirname "$desktop_file")"
        cp -f "dist/linux/${APP_ID}.desktop" "$desktop_file"
    fi
    if [[ ! -f "$icon_file" ]]; then
        mkdir -p "$(dirname "$icon_file")"
        cp -f "dist/linux/hicolor/256x256/apps/${APP_ID}.png" "$icon_file"
    fi

    prepare_linuxdeploy_tools

    rm -f "$OUT_DIR"/*.AppImage 2>/dev/null || true
    pushd "$OUT_DIR" >/dev/null
    linuxdeploy \
        --appdir "$appdir" \
        --executable "$appdir/usr/bin/whatsie" \
        --desktop-file "$desktop_file" \
        --icon-file "$icon_file" \
        --plugin qt \
        --output appimage
    popd >/dev/null

    local generated
    generated="$(ls -1 "$OUT_DIR"/*.AppImage 2>/dev/null | head -1 || true)"
    if [[ -z "$generated" ]]; then
        [[ -f "$APPIMAGETOOL" ]] || fail "AppImage not generated and appimagetool not found."
        chmod +x "$APPIMAGETOOL"
        local fallback="$OUT_DIR/${APP_DISPLAY_NAME}-${PKG_VERSION}-${ARCH}.AppImage"
        "$APPIMAGETOOL" "$appdir" "$fallback"
        generated="$fallback"
    fi

    local target="$OUT_DIR/${APP_DISPLAY_NAME}-${PKG_VERSION}-${ARCH}.AppImage"
    if [[ "$generated" != "$target" ]]; then
        mv -f "$generated" "$target"
    fi
    log "Created: $target"
}

upload_github() {
    command -v gh >/dev/null 2>&1 || fail "gh CLI not found."
    if ! gh auth status >/dev/null 2>&1; then
        fail "gh is not authenticated. Run: gh auth login"
    fi

    local tag="${TAG_NAME}"
    if [[ -z "$tag" ]]; then
        tag="v${PKG_VERSION}"
    fi

    if ! gh release view "$tag" >/dev/null 2>&1; then
        gh release create "$tag" --title "$tag" --notes "Automated release"
    fi

    shopt -s nullglob
    gh release upload "$tag" "$OUT_DIR"/*.deb "$OUT_DIR"/*.AppImage --clobber
    shopt -u nullglob

    log "Uploaded assets to GitHub release: $tag"
}

ensure_build
stage_install

if [[ "$DO_DEB" = true ]]; then
    generate_deb
fi

if [[ "$DO_APPIMAGE" = true ]]; then
    generate_appimage
fi

if [[ "$DO_UPLOAD_GITHUB" = true ]]; then
    upload_github
fi

log "Artifacts in: $OUT_DIR"

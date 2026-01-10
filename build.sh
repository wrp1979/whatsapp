#!/bin/bash
# Build script with auto-incrementing build number
# Usage: ./build.sh [clean|rebuild|run]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$SCRIPT_DIR/src"
BUILD_DIR="$SCRIPT_DIR/build"
BUILD_NUMBER_FILE="$SRC_DIR/BUILD_NUMBER"
JOBS=$(nproc)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[OK]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

increment_build_number() {
    if [[ -f "$BUILD_NUMBER_FILE" ]]; then
        current=$(cat "$BUILD_NUMBER_FILE" | tr -d '[:space:]')
    else
        current=0
    fi
    new=$((current + 1))
    echo "$new" > "$BUILD_NUMBER_FILE"
    log_info "Build number: $current -> $new"

    # Force recompilation of files that use BUILD_NUM
    # Without this, make won't recompile them since source didn't change
    rm -f "$BUILD_DIR/main.o" "$BUILD_DIR/mainwindow.o" 2>/dev/null
}

do_qmake() {
    log_info "Running qmake..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    qmake6 "$SRC_DIR/WhatsApp.pro" -spec linux-g++ CONFIG+=release
    log_success "qmake completed"
}

do_build() {
    log_info "Building with $JOBS parallel jobs..."
    cd "$BUILD_DIR"
    make -j"$JOBS"
    log_success "Build completed successfully"
}

do_clean() {
    log_info "Cleaning build directory..."
    if [[ -d "$BUILD_DIR" ]]; then
        cd "$BUILD_DIR"
        make clean 2>/dev/null || true
        rm -f Makefile .qmake.stash
    fi
    log_success "Clean completed"
}

do_run() {
    if [[ -x "$BUILD_DIR/whatsie" ]]; then
        log_info "Starting whatsie..."
        "$BUILD_DIR/whatsie" &
    else
        log_error "Binary not found. Run ./build.sh first"
        exit 1
    fi
}

show_version() {
    if [[ -x "$BUILD_DIR/whatsie" ]]; then
        "$BUILD_DIR/whatsie" --build-info
    else
        log_error "Binary not found"
    fi
}

# Main logic
case "${1:-}" in
    clean)
        do_clean
        ;;
    rebuild)
        do_clean
        increment_build_number
        do_qmake
        do_build
        show_version
        ;;
    run)
        do_run
        ;;
    version)
        show_version
        ;;
    qmake)
        increment_build_number
        do_qmake
        ;;
    ""|build)
        # Check if Makefile exists, if not run qmake
        if [[ ! -f "$BUILD_DIR/Makefile" ]]; then
            log_warn "Makefile not found, running qmake first..."
            increment_build_number
            do_qmake
        else
            increment_build_number
            # Re-run qmake to pick up new build number
            do_qmake
        fi
        do_build
        show_version
        ;;
    *)
        echo "Usage: $0 [build|clean|rebuild|run|version|qmake]"
        echo ""
        echo "Commands:"
        echo "  build   - Increment build number and compile (default)"
        echo "  clean   - Clean build artifacts"
        echo "  rebuild - Clean, increment, and full rebuild"
        echo "  run     - Run the compiled binary"
        echo "  version - Show current build version"
        echo "  qmake   - Just run qmake (increment build number)"
        exit 1
        ;;
esac

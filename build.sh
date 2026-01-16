#!/bin/bash
# Build script with auto-incrementing build number
# Usage: ./build.sh [clean|rebuild|run]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$SCRIPT_DIR/src"
BUILD_DIR="$SCRIPT_DIR/build"
BUILD_NUMBER_FILE="$SRC_DIR/BUILD_NUMBER"
CHANGELOG_FILE="$SCRIPT_DIR/CHANGELOG.md"
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

generate_changelog() {
    if [[ "${CHANGELOG_DISABLE:-}" == "1" ]]; then
        return
    fi
    if ! git -C "$SCRIPT_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        log_warn "Git repo not detected; skipping CHANGELOG update."
        return
    fi

    local version build_num date header last_tag range
    version="$(awk -F'=' '/^VERSION[[:space:]]*=/{gsub(/ /,"",$2); print $2; exit}' "$SRC_DIR/WhatsApp.pro")"
    build_num="$(tr -d '[:space:]' < "$BUILD_NUMBER_FILE" 2>/dev/null || echo 0)"
    date="$(date -u +%Y-%m-%d)"
    header="## ${version} (build ${build_num}) - ${date}"

    if [[ -f "$CHANGELOG_FILE" ]]; then
        if head -n1 "$CHANGELOG_FILE" | grep -Fq "## ${version} (build ${build_num})"; then
            log_info "CHANGELOG already up to date."
            return
        fi
    fi

    last_tag="$(git -C "$SCRIPT_DIR" tag --list 'v*' --sort=-v:refname | head -n1 || true)"
    if [[ -n "$last_tag" ]]; then
        range="${last_tag}..HEAD"
    else
        range="HEAD~50..HEAD"
    fi

    mapfile -t subjects < <(git -C "$SCRIPT_DIR" log --pretty=format:%s "$range")
    if [[ "${#subjects[@]}" -eq 0 ]]; then
        log_warn "No commits found for CHANGELOG."
    fi

    local -a features fixes perf refactors docs builds tests chores other
    for line in "${subjects[@]}"; do
        [[ "$line" =~ ^Merge ]] && continue
        [[ "$line" =~ ^docs\\(changelog\\) ]] && continue
        type=$(echo "$line" | sed -n 's/^\([a-zA-Z]*\)[(:!].*$/\1/p')
        type="${type,,}"
        case "$type" in
            feat) features+=("$line") ;;
            fix) fixes+=("$line") ;;
            perf) perf+=("$line") ;;
            refactor) refactors+=("$line") ;;
            docs) docs+=("$line") ;;
            build|ci) builds+=("$line") ;;
            test) tests+=("$line") ;;
            chore|style) chores+=("$line") ;;
            *) other+=("$line") ;;
        esac
    done

    local tmp new
    tmp="$(mktemp)"
    new="$(mktemp)"
    {
        echo "$header"
        echo ""
        if [[ "${#features[@]}" -gt 0 ]]; then
            echo "#### 🎁 Feature"
            printf '* %s\n' "${features[@]}"
            echo ""
        fi
        if [[ "${#fixes[@]}" -gt 0 ]]; then
            echo "#### 🐞 Bug Fixes"
            printf '* %s\n' "${fixes[@]}"
            echo ""
        fi
        if [[ "${#perf[@]}" -gt 0 ]]; then
            echo "#### 🚀 Performance"
            printf '* %s\n' "${perf[@]}"
            echo ""
        fi
        if [[ "${#refactors[@]}" -gt 0 ]]; then
            echo "#### 🧹 Refactor"
            printf '* %s\n' "${refactors[@]}"
            echo ""
        fi
        if [[ "${#docs[@]}" -gt 0 ]]; then
            echo "#### 📚 Docs"
            printf '* %s\n' "${docs[@]}"
            echo ""
        fi
        if [[ "${#builds[@]}" -gt 0 ]]; then
            echo "#### 🛠 Build/CI"
            printf '* %s\n' "${builds[@]}"
            echo ""
        fi
        if [[ "${#tests[@]}" -gt 0 ]]; then
            echo "#### 🧪 Tests"
            printf '* %s\n' "${tests[@]}"
            echo ""
        fi
        if [[ "${#chores[@]}" -gt 0 ]]; then
            echo "#### 🚧 Chores"
            printf '* %s\n' "${chores[@]}"
            echo ""
        fi
        if [[ "${#features[@]}" -eq 0 && "${#fixes[@]}" -eq 0 && "${#perf[@]}" -eq 0 && \
              "${#refactors[@]}" -eq 0 && "${#docs[@]}" -eq 0 && "${#builds[@]}" -eq 0 && \
              "${#tests[@]}" -eq 0 && "${#chores[@]}" -eq 0 && "${#other[@]}" -eq 0 ]]; then
            echo "#### 🚧 Chores"
            echo "* No changes since last release."
            echo ""
        elif [[ "${#other[@]}" -gt 0 ]]; then
            echo "#### 🚧 Chores"
            printf '* %s\n' "${other[@]}"
            echo ""
        fi
    } > "$new"

    if [[ -f "$CHANGELOG_FILE" ]]; then
        { cat "$new"; echo ""; cat "$CHANGELOG_FILE"; } > "$tmp"
    else
        cat "$new" > "$tmp"
    fi
    mv "$tmp" "$CHANGELOG_FILE"
    rm -f "$new"
    log_success "CHANGELOG updated."
}

increment_build_number() {
    if [[ -n "${BUILD_NUMBER_OVERRIDE:-}" ]]; then
        echo "$BUILD_NUMBER_OVERRIDE" > "$BUILD_NUMBER_FILE"
        log_info "Build number set by override: $BUILD_NUMBER_OVERRIDE"

        # Force recompilation of files that use BUILD_NUM
        rm -f "$BUILD_DIR/main.o" "$BUILD_DIR/mainwindow.o" 2>/dev/null
        generate_changelog
        return
    fi
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
    generate_changelog
}

do_qmake() {
    log_info "Running qmake..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    local qmake_bin="${QMAKE_BIN:-}"
    if [[ -z "$qmake_bin" ]]; then
        qmake_bin=$(command -v qmake6 || command -v qmake || true)
    fi
    if [[ -z "$qmake_bin" ]]; then
        log_error "qmake6/qmake not found. Install Qt6 or set QMAKE_BIN."
        exit 1
    fi
    "$qmake_bin" "$SRC_DIR/WhatsApp.pro" -spec linux-g++ CONFIG+=release
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

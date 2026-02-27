#!/usr/bin/env bash
# setup.sh — Build and test PlantBrainGrid
#
# Usage:
#   ./setup.sh                  Full build: C++ + Python bindings (no tests)
#   ./setup.sh --test           Also run C++ and Python tests after building
#   ./setup.sh --skip-python    C++ only (no Python bindings or pytest)
#   ./setup.sh --raylib         Also install raylib for visual mode
#   ./setup.sh --clean          Remove build dirs and venv first
#   ./setup.sh --help           Show this message

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ─── Colours ─────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; BOLD='\033[1m'; RESET='\033[0m'

info()  { echo -e "${BLUE}[INFO]${RESET}  $*"; }
ok()    { echo -e "${GREEN}[ OK ]${RESET}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
err()   { echo -e "${RED}[ERR ]${RESET}  $*" >&2; }
die()   { err "$*"; exit 1; }
step()  { echo -e "\n${BOLD}━━━  $*  ━━━${RESET}"; }

# ─── Flags ───────────────────────────────────────────────────────────────────
SKIP_PYTHON=0
RUN_TESTS=0
INSTALL_RAYLIB=0
CLEAN=0

for arg in "$@"; do
    case $arg in
        --skip-python)  SKIP_PYTHON=1 ;;
        --test)         RUN_TESTS=1 ;;
        --raylib)       INSTALL_RAYLIB=1 ;;
        --clean)        CLEAN=1 ;;
        --help|-h)
            sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) die "Unknown option: $arg  (try --help)" ;;
    esac
done

# ─── Clean ───────────────────────────────────────────────────────────────────
if [[ $CLEAN -eq 1 ]]; then
    step "Cleaning build artefacts"
    rm -rf "$SCRIPT_DIR/build" "$SCRIPT_DIR/venv"
    rm -f  "$SCRIPT_DIR"/_plantbraingrid*.so
    rm -f  "$SCRIPT_DIR/src/python/plantbraingrid/"_plantbraingrid*.so
    ok "Clean complete"
fi

# ─── 1. Prerequisites ─────────────────────────────────────────────────────────
step "Checking prerequisites"

missing=()
for cmd in cmake make python3; do
    if command -v "$cmd" >/dev/null 2>&1; then
        ok "$cmd  $(command -v "$cmd")"
    else
        missing+=("$cmd")
    fi
done

[[ ${#missing[@]} -eq 0 ]] || die "Missing required tools: ${missing[*]}"

# Detect Python version (e.g. "3.12")
PY_VER=$(python3 -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')")
info "Python version: $PY_VER"

# ─── 2. C++ build ────────────────────────────────────────────────────────────
step "Building C++ core"

mkdir -p "$SCRIPT_DIR/build"
cd "$SCRIPT_DIR/build"

cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_PYTHON_BINDINGS=OFF \
    -DBUILD_TESTS="$( [[ $RUN_TESTS -eq 1 ]] && echo ON || echo OFF )" \
    -Wno-dev \
    2>&1 | grep -E "^(-- |CMake|Error|fatal)" || true

if [[ $RUN_TESTS -eq 1 ]]; then
    make -j"$(nproc)" plantbraingrid_core plantbraingrid_tests
else
    make -j"$(nproc)" plantbraingrid_core
fi
ok "C++ build complete"

if [[ $RUN_TESTS -eq 1 ]]; then
    step "Running C++ tests"
    ctest --output-on-failure
    ok "All C++ tests passed"
fi

cd "$SCRIPT_DIR"

[[ $SKIP_PYTHON -eq 1 ]] && { echo -e "\n${GREEN}${BOLD}C++ setup complete (Python skipped).${RESET}"; exit 0; }

# ─── 3. Python virtual environment ───────────────────────────────────────────
step "Setting up Python virtual environment"

VENV="$SCRIPT_DIR/venv"
if [[ ! -d "$VENV" ]]; then
    python3 -m venv "$VENV"
    ok "Created venv at $VENV"
else
    info "Reusing existing venv at $VENV"
fi

PIP="$VENV/bin/pip"
"$PIP" install --quiet --upgrade pip
"$PIP" install --quiet pybind11 pytest
ok "pip packages: pybind11, pytest"

if [[ $INSTALL_RAYLIB -eq 1 ]]; then
    "$PIP" install --quiet raylib
    ok "pip packages: raylib"
fi

# ─── 4. Python development headers ───────────────────────────────────────────
step "Locating Python $PY_VER development headers"

# Ask Python itself where its include dir is
PY_SYSINCLUDE=$(python3 -c "import sysconfig; print(sysconfig.get_path('include'))")
CMAKE_PY_ARGS=()

if [[ -f "$PY_SYSINCLUDE/Python.h" ]]; then
    ok "System headers found: $PY_SYSINCLUDE"
    # Also look for a matching shared library
    PY_LIB=$(python3 -c "
import sysconfig, os
ld = sysconfig.get_config_var('LDLIBRARY') or ''
for d in [sysconfig.get_config_var('LIBDIR'), '/usr/lib', '/usr/lib/x86_64-linux-gnu']:
    if d:
        p = os.path.join(d, ld)
        if os.path.exists(p):
            print(p); break
" 2>/dev/null || true)
    [[ -n "$PY_LIB" ]] && CMAKE_PY_ARGS+=(-DPython3_LIBRARY="$PY_LIB")
    CMAKE_PY_ARGS+=(-DPython3_EXECUTABLE="$(command -v python3)")
else
    warn "Python.h not found at $PY_SYSINCLUDE"
    info "Attempting to download python${PY_VER}-dev headers (no sudo needed)..."

    # Require dpkg tools
    command -v dpkg-deb >/dev/null 2>&1 \
        || die "dpkg-deb not found. Install python${PY_VER}-dev manually:\n  sudo apt-get install python${PY_VER}-dev"

    # Determine multiarch triple (e.g. x86_64-linux-gnu) without dpkg-architecture
    DPKG_ARCH=$(dpkg --print-architecture 2>/dev/null || echo "")
    case "$DPKG_ARCH" in
        amd64)   MULTIARCH="x86_64-linux-gnu" ;;
        arm64)   MULTIARCH="aarch64-linux-gnu" ;;
        armhf)   MULTIARCH="arm-linux-gnueabihf" ;;
        i386)    MULTIARCH="i386-linux-gnu" ;;
        *)       MULTIARCH="$(uname -m)-linux-gnu" ;;
    esac

    PYDEV_WORK="/tmp/pbg_pydev_${PY_VER//./}"
    DEB_DIR="$PYDEV_WORK/debs"
    EXTRACTED="$PYDEV_WORK/extracted"
    PY_PREFIX="$PYDEV_WORK/prefix"

    # Only re-extract if Python.h is missing
    if [[ ! -f "$PY_PREFIX/include/python${PY_VER}/Python.h" ]]; then
        mkdir -p "$DEB_DIR" "$EXTRACTED" "$PY_PREFIX/include/python${PY_VER}"

        info "Downloading python${PY_VER}-dev .deb packages..."
        (cd "$DEB_DIR" && apt-get download "python${PY_VER}-dev" "libpython${PY_VER}-dev" 2>&1) \
            || die "apt-get download failed. Try:\n  sudo apt-get install python${PY_VER}-dev"

        for deb in "$DEB_DIR"/*.deb; do
            dpkg-deb -x "$deb" "$EXTRACTED/"
        done

        # Copy main headers
        SRC_INC="$EXTRACTED/usr/include/python${PY_VER}"
        [[ -d "$SRC_INC" ]] || die "Expected include dir not found after extraction: $SRC_INC"
        cp -r "$SRC_INC/." "$PY_PREFIX/include/python${PY_VER}/"

        # Copy multiarch-specific pyconfig.h (Ubuntu stores it separately)
        ARCH_INC="$EXTRACTED/usr/include/$MULTIARCH/python${PY_VER}"
        if [[ -d "$ARCH_INC" ]]; then
            mkdir -p "$PY_PREFIX/include/$MULTIARCH"
            cp -r "$ARCH_INC" "$PY_PREFIX/include/$MULTIARCH/"
            ln -sf \
                "$PY_PREFIX/include/$MULTIARCH/python${PY_VER}/pyconfig.h" \
                "$PY_PREFIX/include/python${PY_VER}/pyconfig.h" \
                2>/dev/null || true
        fi

        ok "Headers extracted to $PY_PREFIX"
    else
        info "Reusing cached headers at $PY_PREFIX"
    fi

    # Locate libpython — prefer the extracted copy, fall back to system .so
    PY_LIB=$(find "$EXTRACTED" -name "libpython${PY_VER}*.so" 2>/dev/null | head -1)
    if [[ -z "$PY_LIB" ]]; then
        PY_LIB=$(find /usr/lib -name "libpython${PY_VER}*.so" 2>/dev/null | head -1)
    fi
    [[ -n "$PY_LIB" ]] || die "Could not find libpython${PY_VER}.so anywhere"

    PY_INCLUDE="$PY_PREFIX/include/python${PY_VER}"
    CMAKE_PY_ARGS=(
        -DPython3_INCLUDE_DIR="$PY_INCLUDE"
        -DPython3_LIBRARY="$PY_LIB"
        -DPython3_EXECUTABLE="$(command -v python3)"
    )
fi

# ─── 5. Python bindings ───────────────────────────────────────────────────────
step "Building Python bindings (_plantbraingrid)"

mkdir -p "$SCRIPT_DIR/build"
cd "$SCRIPT_DIR/build"

cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_PYTHON_BINDINGS=ON \
    -DBUILD_TESTS=OFF \
    "${CMAKE_PY_ARGS[@]}" \
    -Wno-dev \
    2>&1 | grep -E "^(-- |CMake|Error|fatal)" || true

make -j"$(nproc)" _plantbraingrid

SO_FILE=$(find . -name "_plantbraingrid*.so" -maxdepth 2 | head -1)
[[ -n "$SO_FILE" ]] || die "Build succeeded but _plantbraingrid*.so not found"

cp "$SO_FILE" "$SCRIPT_DIR/src/python/plantbraingrid/"
cp "$SO_FILE" "$SCRIPT_DIR/"
ok "Installed: $(basename "$SO_FILE")"

cd "$SCRIPT_DIR"

# ─── 6. Python tests ──────────────────────────────────────────────────────────
if [[ $RUN_TESTS -eq 1 ]]; then
    step "Running Python tests"
    PYTHONPATH="$SCRIPT_DIR:$SCRIPT_DIR/src/python" \
        "$VENV/bin/pytest" tests/python/ -v
    ok "All Python tests passed"
fi

# ─── Done ─────────────────────────────────────────────────────────────────────
echo
echo -e "${GREEN}${BOLD}Setup complete!${RESET}"
echo
echo "Run the simulation:"
echo "  Headless (fixed seed, 500 ticks):"
echo "    PYTHONPATH=src/python python -m plantbraingrid --headless --seed 42 --ticks 500"
echo
echo "  Headless with auto-spawn (start from nothing):"
echo "    PYTHONPATH=src/python python -m plantbraingrid --headless --auto-spawn --ticks 2000"
echo
if [[ $INSTALL_RAYLIB -eq 1 ]]; then
    echo "  Visual (raylib):"
    echo "    PYTHONPATH=src/python $VENV/bin/python -m plantbraingrid --auto-spawn"
    echo
fi
echo "Run tests (or re-run setup with --test):"
echo "  C++:    cd build && ctest --output-on-failure"
echo "  Python: PYTHONPATH=$SCRIPT_DIR:$SCRIPT_DIR/src/python $VENV/bin/pytest tests/python/ -v"
echo
echo "Assemble a brain:"
echo "  python tools/brain_assembler.py examples/simple_leaf_grower.asm --hex"

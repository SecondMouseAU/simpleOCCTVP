#!/bin/bash
#
# Build OpenCASCADE 8.0.0 as static libraries for simpleOCCTVP
#
# Usage: ./build-occt-deps.sh [--platform macos|windows|linux]
#
# This script downloads OCCT source and builds it as static libraries.
# The result is installed into deps/occt-install/ for use by CMakeLists.txt.
#
# Prerequisites:
#   - CMake 3.20+ (brew install cmake / apt install cmake)
#   - C++17 compiler (clang or gcc)
#   - ~5GB free disk space
#
# Build time: ~15-30 minutes depending on hardware
#

set -e

OCCT_VERSION="8.0.0"
OCCT_RC="rc3"
# RC tags use format V8_0_0_rc3, release uses V8_0_0
if [ -n "$OCCT_RC" ]; then
    OCCT_TAG="V${OCCT_VERSION//./_}_${OCCT_RC}"
else
    OCCT_TAG="V${OCCT_VERSION//./_}"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DEPS_DIR="$PROJECT_DIR/deps"

# Parse platform argument
PLATFORM="${1:-auto}"
if [ "$PLATFORM" = "--platform" ]; then
    PLATFORM="${2:-auto}"
fi

# Auto-detect platform
if [ "$PLATFORM" = "auto" ]; then
    case "$(uname -s)" in
        Darwin*)  PLATFORM="macos" ;;
        Linux*)   PLATFORM="linux" ;;
        MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
        *)        echo "Unknown platform: $(uname -s)"; exit 1 ;;
    esac
fi

# Parallelism
if [ "$PLATFORM" = "macos" ]; then
    JOBS=$(sysctl -n hw.ncpu)
elif [ "$PLATFORM" = "windows" ]; then
    JOBS=${NUMBER_OF_PROCESSORS:-4}
else
    JOBS=$(nproc 2>/dev/null || echo 4)
fi

echo "========================================"
echo "Building OCCT $OCCT_VERSION for $PLATFORM"
echo "========================================"
echo "Project directory: $PROJECT_DIR"
echo "Dependencies directory: $DEPS_DIR"
echo "Parallel jobs: $JOBS"
echo ""

mkdir -p "$DEPS_DIR"
cd "$DEPS_DIR"

# --------------------
# Download OCCT source
# --------------------

if [ ! -d "occt-src" ]; then
    echo ">>> Downloading OCCT source..."
    if [ -n "$OCCT_RC" ]; then
        git clone --depth 1 --branch "$OCCT_TAG" \
            https://github.com/Open-Cascade-SAS/OCCT.git occt-src
    else
        git clone --depth 1 --branch "$OCCT_TAG" \
            https://git.dev.opencascade.org/repos/occt.git occt-src
    fi
else
    echo ">>> OCCT source already exists, skipping download"
fi

# --------------------
# Common CMake options (I/O + healing + meshing + visualization)
# --------------------

CMAKE_COMMON_OPTS=(
    -DBUILD_LIBRARY_TYPE=Static
    -DBUILD_MODULE_ApplicationFramework=OFF
    -DBUILD_MODULE_DataExchange=ON
    -DBUILD_MODULE_Draw=OFF
    -DBUILD_MODULE_FoundationClasses=ON
    -DBUILD_MODULE_ModelingAlgorithms=ON
    -DBUILD_MODULE_ModelingData=ON
    -DBUILD_MODULE_Visualization=ON
    -DBUILD_SAMPLES_QT=OFF
    -DBUILD_DOC_Overview=OFF
    -DBUILD_PATCH=OFF
    -DUSE_FREETYPE=OFF
    -DUSE_FREEIMAGE=OFF
    -DUSE_RAPIDJSON=OFF
    -DUSE_TBB=OFF
    -DUSE_VTK=OFF
    -DUSE_OPENGL=ON
    -DUSE_GLES2=OFF
    -DUSE_D3D=OFF
    -DUSE_DRACO=OFF
    -DUSE_FFMPEG=OFF
    -DUSE_OPENVR=OFF
    -DUSE_XLIB=OFF
    -DUSE_TCL=OFF
    -DINSTALL_SAMPLES=OFF
    -DINSTALL_TEST_CASES=OFF
    -DINSTALL_DOC_Overview=OFF
    -DCMAKE_CXX_STANDARD=17
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX="$DEPS_DIR/occt-install"
)

# --------------------
# Platform-specific build
# --------------------

echo ""
echo ">>> Building for $PLATFORM..."
rm -rf occt-build
mkdir -p occt-build
cd occt-build

case "$PLATFORM" in
    macos)
        MACOS_SDK=$(xcrun --sdk macosx --show-sdk-path)
        CC=$(xcrun --find clang)
        CXX=$(xcrun --find clang++)
        MACOS_FLAGS="-arch arm64 -isysroot $MACOS_SDK -mmacosx-version-min=12.0"

        cmake ../occt-src \
            -G "Unix Makefiles" \
            "${CMAKE_COMMON_OPTS[@]}" \
            -DCMAKE_OSX_SYSROOT="$MACOS_SDK" \
            -DCMAKE_OSX_ARCHITECTURES=arm64 \
            -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
            -DCMAKE_C_COMPILER="$CC" \
            -DCMAKE_CXX_COMPILER="$CXX" \
            -DCMAKE_C_FLAGS="$MACOS_FLAGS" \
            -DCMAKE_CXX_FLAGS="$MACOS_FLAGS"
        ;;

    linux)
        cmake ../occt-src \
            -G "Unix Makefiles" \
            "${CMAKE_COMMON_OPTS[@]}" \
            -DUSE_XLIB=ON \
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        ;;

    windows)
        # Use Visual Studio generator if available, otherwise NMake
        if command -v cl.exe &> /dev/null; then
            cmake ../occt-src \
                -G "NMake Makefiles" \
                "${CMAKE_COMMON_OPTS[@]}"
        else
            cmake ../occt-src \
                -G "Visual Studio 17 2022" \
                -A x64 \
                "${CMAKE_COMMON_OPTS[@]}"
        fi
        ;;
esac

# Build (ignore executable link failures — we only need static libs)
if [ "$PLATFORM" = "windows" ]; then
    cmake --build . --config Release --parallel "$JOBS" || true
    cmake --install . --config Release || true
else
    cmake --build . --parallel "$JOBS" || true
    cmake --install . || true
fi
cd ..

# --------------------
# Verify installation
# --------------------

echo ""
echo ">>> Verifying installation..."

MISSING=0
for LIB in TKernel TKMath TKBRep TKDESTL TKDESTEP TKShHealing TKMesh TKOpenGl; do
    if [ "$PLATFORM" = "windows" ]; then
        FOUND=$(find "$DEPS_DIR/occt-install" -name "${LIB}.lib" 2>/dev/null | head -1)
    else
        FOUND=$(find "$DEPS_DIR/occt-install" -name "lib${LIB}.a" 2>/dev/null | head -1)
    fi
    if [ -n "$FOUND" ]; then
        echo "  OK: $LIB"
    else
        echo "  MISSING: $LIB"
        MISSING=$((MISSING + 1))
    fi
done

if [ "$MISSING" -gt 0 ]; then
    echo ""
    echo "WARNING: $MISSING libraries missing. Build may have partially failed."
    echo "Check the build output above for errors."
fi

# --------------------
# Summary
# --------------------

echo ""
echo "========================================"
echo "OCCT build complete!"
echo "========================================"
echo ""
echo "Installed to: $DEPS_DIR/occt-install"
echo ""
if [ -d "$DEPS_DIR/occt-install/lib" ]; then
    LIB_COUNT=$(find "$DEPS_DIR/occt-install/lib" -name "*.a" -o -name "*.lib" 2>/dev/null | wc -l)
    echo "Static libraries: $LIB_COUNT"
fi
if [ -d "$DEPS_DIR/occt-install/include" ]; then
    HDR_COUNT=$(find "$DEPS_DIR/occt-install/include" -name "*.hxx" -o -name "*.h" 2>/dev/null | wc -l)
    echo "Header files: $HDR_COUNT"
fi
echo ""
echo "Next step: cd .. && cmake -B build && cmake --build build"
echo ""

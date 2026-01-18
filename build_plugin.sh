#!/bin/bash

# ============================================================================
# Giant Instruments Multi-Format Build Script
# ============================================================================
#
# Builds all 5 desktop plugin formats (VST3, AU, CLAP, LV2, Standalone)
# AUv3 must be built separately using the iOS build script in plugins/AUv3/
#
# Usage: ./build_plugin.sh [clean]
#
# ============================================================================

set -e  # Exit on error

# ============================================================================
# Configuration
# ============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/.build/cmake"
INSTALL_DIR="${SCRIPT_DIR}/plugins"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# ============================================================================
# Functions
# ============================================================================

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_banner() {
    echo ""
    echo "============================================================================"
    echo "  Giant Instruments Multi-Format Build Script"
    echo "============================================================================"
    echo ""
}

# ============================================================================
# Pre-flight Checks
# ============================================================================

check_prerequisites() {
    log_info "Checking prerequisites..."

    # Check for CMake
    if ! command -v cmake &> /dev/null; then
        log_error "CMake not found. Please install CMake."
        exit 1
    fi

    # Check for Xcode (macOS)
    if [[ "$OSTYPE" == "darwin"* ]]; then
        if ! command -v xcodebuild &> /dev/null; then
            log_error "Xcode not found. Please install Xcode."
            exit 1
        fi
    fi

    log_success "Prerequisites check passed"
}

# ============================================================================
# Clean Build
# ============================================================================

clean_build() {
    log_info "Cleaning build directory..."
    rm -rf "${BUILD_DIR}"
    log_success "Build directory cleaned"
}

# ============================================================================
# Build Configuration
# ============================================================================

configure_build() {
    log_info "Configuring CMake build..."

    mkdir -p "${BUILD_DIR}"

    # Find JUCE
    JUCE_PATH=""
    for path in \
        "${SCRIPT_DIR}/../external/JUCE" \
        "${SCRIPT_DIR}/external/JUCE" \
        ~/JUCE \
        /usr/local/JUCE
    do
        if [ -d "$path" ]; then
            JUCE_PATH="$path"
            break
        fi
    done

    if [ -z "$JUCE_PATH" ]; then
        log_error "JUCE not found. Please install JUCE."
        exit 1
    fi

    log_success "Found JUCE at: ${JUCE_PATH}"

    # Configure CMake
    cd "${BUILD_DIR}"
    cmake "${SCRIPT_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES=arm64;x86_64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15

    log_success "CMake configuration complete"
}

# ============================================================================
# Build Plugins
# ============================================================================

build_plugins() {
    log_info "Building plugins (this may take a while)..."

    cd "${BUILD_DIR}"

    # Build all formats
    cmake --build . --config Release --parallel $(sysctl -n hw.ncpu)

    log_success "Plugin build complete"
}

# ============================================================================
# Install Plugins
# ============================================================================

install_plugins() {
    log_info "Installing plugins to plugins/ folder..."

    # Create plugin directories
    mkdir -p "${INSTALL_DIR}"/{vst,au,clap,lv2,standalone}

    # Copy VST3
    if [ -d "${BUILD_DIR}/VST3/Release" ]; then
        cp -R "${BUILD_DIR}/VST3/Release/"*.vst3 "${INSTALL_DIR}/vst/" 2>/dev/null || true
        log_success "VST3 plugin installed"
    fi

    # Copy AU
    if [ -d "${BUILD_DIR}/AU/Release" ]; then
        cp -R "${BUILD_DIR}/AU/Release/"*.component "${INSTALL_DIR}/au/" 2>/dev/null || true
        log_success "AU plugin installed"
    fi

    # Copy CLAP
    if [ -d "${BUILD_DIR}/CLAP/Release" ]; then
        cp -R "${BUILD_DIR}/CLAP/Release/"*.clap "${INSTALL_DIR}/clap/" 2>/dev/null || true
        log_success "CLAP plugin installed"
    fi

    # Copy LV2
    if [ -d "${BUILD_DIR}/LV2/Release" ]; then
        cp -R "${BUILD_DIR}/LV2/Release/"* "${INSTALL_DIR}/lv2/" 2>/dev/null || true
        log_success "LV2 plugin installed"
    fi

    # Copy Standalone
    if [ -d "${BUILD_DIR}/Standalone/Release" ]; then
        cp -R "${BUILD_DIR}/Standalone/Release/"*.app "${INSTALL_DIR}/standalone/" 2>/dev/null || true
        log_success "Standalone app installed"
    fi

    log_success "All plugins installed to plugins/ folder"
}

# ============================================================================
# Validate Build
# ============================================================================

validate_build() {
    log_info "Validating build..."

    local missing=0

    # Check VST3
    if [ ! -d "${INSTALL_DIR}/vst/" ] || [ -z "$(ls -A ${INSTALL_DIR}/vst/)" ]; then
        log_warning "VST3 plugin not found"
        ((missing++))
    fi

    # Check AU
    if [ ! -d "${INSTALL_DIR}/au/" ] || [ -z "$(ls -A ${INSTALL_DIR}/au/)" ]; then
        log_warning "AU plugin not found"
        ((missing++))
    fi

    # Check CLAP
    if [ ! -d "${INSTALL_DIR}/clap/" ] || [ -z "$(ls -A ${INSTALL_DIR}/clap/)" ]; then
        log_warning "CLAP plugin not found"
        ((missing++))
    fi

    # Check LV2
    if [ ! -d "${INSTALL_DIR}/lv2/" ] || [ -z "$(ls -A ${INSTALL_DIR}/lv2/)" ]; then
        log_warning "LV2 plugin not found"
        ((missing++))
    fi

    # Check Standalone
    if [ ! -d "${INSTALL_DIR}/standalone/" ] || [ -z "$(ls -A ${INSTALL_DIR}/standalone/)" ]; then
        log_warning "Standalone app not found"
        ((missing++))
    fi

    if [ $missing -gt 0 ]; then
        log_warning "Build validation complete: $missing formats missing"
        log_info "Note: Some formats may not be supported on this platform"
    else
        log_success "Build validation complete: All formats built successfully"
    fi
}

# ============================================================================
# Main Build Process
# ============================================================================

main() {
    print_banner

    # Check if clean build requested
    if [ "$1" == "clean" ]; then
        clean_build
    fi

    check_prerequisites
    configure_build
    build_plugins
    install_plugins
    validate_build

    echo ""
    echo "============================================================================"
    log_success "Build process complete!"
    echo "============================================================================"
    echo ""
    log_info "Plugin locations:"
    echo "  VST3:      ${INSTALL_DIR}/vst/"
    echo "  AU:        ${INSTALL_DIR}/au/"
    echo "  CLAP:      ${INSTALL_DIR}/clap/"
    echo "  LV2:       ${INSTALL_DIR}/lv2/"
    echo "  Standalone: ${INSTALL_DIR}/standalone/"
    echo ""
    log_info "Note: AUv3 must be built separately using:"
    echo "  cd plugins/AUv3 && ./build.sh"
    echo ""
    log_info "Next steps:"
    echo "  1. Copy plugins to your DAW's plugin folder"
    echo "  2. Test in REAPER (VST3)"
    echo "  3. Test in Logic Pro (AU)"
    echo "  4. Test in GarageBand (AUv3)"
    echo "============================================================================"
    echo ""
}

# Run main function
main "$@"

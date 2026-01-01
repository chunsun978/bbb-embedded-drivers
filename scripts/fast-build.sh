#!/bin/bash
#
# Fast Iteration Build Script for BBB Driver Development
#
# This script builds kernel modules WITHOUT running full Yocto builds.
# Uses the Yocto-generated toolchain and kernel build artifacts directly.
#
# Usage:
#   ./fast-build.sh button   # Build button driver
#   ./fast-build.sh tmp117   # Build TMP117 driver
#   ./fast-build.sh all      # Build all drivers
#   ./fast-build.sh clean    # Clean all builds
#

set -e

# ============================================================================
# CONFIGURATION - Paths from Yocto build
# ============================================================================

YOCTO_BUILD="/home/chun/yocto/beaglebone/build"
KERNEL_VERSION="6.1.80+gitAUTOINC+4ca9ea3076-r0"

# Toolchain path
TOOLCHAIN_BIN="${YOCTO_BUILD}/tmp/work/beaglebone-poky-linux-gnueabi/linux-bb.org/${KERNEL_VERSION}/recipe-sysroot-native/usr/bin/arm-poky-linux-gnueabi"

# Kernel source and build directories
KERNEL_SRC="${YOCTO_BUILD}/tmp/work-shared/beaglebone/kernel-source"
KERNEL_BUILD="${YOCTO_BUILD}/tmp/work-shared/beaglebone/kernel-build-artifacts"

# Cross compiler prefix
CROSS_COMPILE="${TOOLCHAIN_BIN}/arm-poky-linux-gnueabi-"

# Project root
PROJECT_ROOT="/home/chun/projects/buildBBBWithYocto"

# Driver directories
BUTTON_DIR="${PROJECT_ROOT}/kernel/drivers/button"
TMP117_DIR="${PROJECT_ROOT}/kernel/drivers/tmp117"
ADC_DIR="${PROJECT_ROOT}/kernel/drivers/adc"

# BBB connection
BBB_IP="192.168.86.21"
BBB_USER="root"

# ============================================================================
# COLORS
# ============================================================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# ============================================================================
# FUNCTIONS
# ============================================================================

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

check_toolchain() {
    if [ ! -f "${CROSS_COMPILE}gcc" ]; then
        log_error "Toolchain not found at: ${CROSS_COMPILE}gcc"
        log_info "Please ensure Yocto build has completed at least once."
        exit 1
    fi
    log_success "Toolchain found: $(${CROSS_COMPILE}gcc --version | head -1)"
}

check_kernel_build() {
    if [ ! -f "${KERNEL_BUILD}/Module.symvers" ]; then
        log_error "Kernel build artifacts not found at: ${KERNEL_BUILD}"
        log_info "Please ensure Yocto kernel build has completed."
        exit 1
    fi
    log_success "Kernel build artifacts found"
}

build_driver() {
    local driver_name=$1
    local driver_dir=$2
    
    log_info "Building ${driver_name} driver..."
    
    cd "${driver_dir}"
    
    # Clean first
    make -C "${KERNEL_BUILD}" M="${driver_dir}" clean 2>/dev/null || true
    
    # Build with proper environment
    make -C "${KERNEL_BUILD}" \
        M="${driver_dir}" \
        ARCH=arm \
        CROSS_COMPILE="${CROSS_COMPILE}" \
        modules
    
    if [ -f "${driver_dir}/"*.ko ]; then
        log_success "${driver_name} driver built successfully!"
        ls -la "${driver_dir}/"*.ko
    else
        log_error "Failed to build ${driver_name} driver"
        exit 1
    fi
}

build_button() {
    build_driver "button" "${BUTTON_DIR}"
}

build_tmp117() {
    build_driver "tmp117" "${TMP117_DIR}"
}

build_adc() {
    build_driver "adc" "${ADC_DIR}"
}

build_all() {
    build_button
    echo ""
    build_tmp117
    echo ""
    build_adc
}

clean_all() {
    log_info "Cleaning all driver builds..."
    
    for dir in "${BUTTON_DIR}" "${TMP117_DIR}" "${ADC_DIR}"; do
        if [ -d "$dir" ]; then
            make -C "${KERNEL_BUILD}" M="${dir}" clean 2>/dev/null || true
            log_success "Cleaned: ${dir}"
        fi
    done
}

deploy_driver() {
    local driver_name=$1
    local driver_dir=$2
    local ko_file=$(ls "${driver_dir}/"*.ko 2>/dev/null | head -1)
    
    if [ -z "$ko_file" ]; then
        log_error "No .ko file found in ${driver_dir}. Build first!"
        exit 1
    fi
    
    log_info "Deploying ${driver_name} to BBB (${BBB_IP})..."
    
    # Copy to BBB
    scp "${ko_file}" "${BBB_USER}@${BBB_IP}:/tmp/"
    
    local ko_basename=$(basename "${ko_file}")
    local module_name="${ko_basename%.ko}"
    
    # Special handling for button driver (unbind if already loaded)
    if [ "$driver_name" == "button" ]; then
        log_info "Handling button driver reload..."
        ssh "${BBB_USER}@${BBB_IP}" "
            # Try unbind first if device is bound
            if [ -d /sys/bus/platform/drivers/bbb_flagship_button ]; then
                echo 'bbb-flagship-button' > /sys/bus/platform/drivers/bbb_flagship_button/unbind 2>/dev/null || true
            fi
            # Remove old modules
            rmmod bbb_flagship_button 2>/dev/null || true
            rmmod bbb_flagship_button_combined 2>/dev/null || true
            # Load new module
            insmod /tmp/${ko_basename}
            lsmod | grep bbb_flagship_button
        "
    else
        # Standard deployment for other drivers
        ssh "${BBB_USER}@${BBB_IP}" "
            rmmod ${module_name} 2>/dev/null || true
            insmod /tmp/${ko_basename}
            lsmod | grep ${module_name}
        "
    fi
    
    log_success "${driver_name} deployed and loaded!"
}

show_usage() {
    echo "Fast Iteration Build Script for BBB Driver Development"
    echo ""
    echo "Usage: $0 <command>"
    echo ""
    echo "Build Commands:"
    echo "  button        Build button driver"
    echo "  tmp117        Build TMP117 driver"
    echo "  adc           Build MCP3008 ADC driver"
    echo "  all           Build all drivers"
    echo "  clean         Clean all builds"
    echo ""
    echo "Deploy Commands:"
    echo "  deploy-button Deploy button driver to BBB"
    echo "  deploy-tmp117 Deploy TMP117 driver to BBB"
    echo "  deploy-adc    Deploy MCP3008 ADC driver to BBB"
    echo ""
    echo "Build + Deploy:"
    echo "  button-deploy Build and deploy button driver"
    echo "  tmp117-deploy Build and deploy TMP117 driver"
    echo "  adc-deploy    Build and deploy MCP3008 ADC driver"
    echo ""
    echo "Info:"
    echo "  info          Show configuration info"
    echo "  help          Show this help"
    echo ""
    echo "Examples:"
    echo "  $0 button           # Just build"
    echo "  $0 button-deploy    # Build and deploy to BBB"
}

show_info() {
    echo "=== Fast Build Configuration ==="
    echo ""
    echo "Toolchain: ${CROSS_COMPILE}gcc"
    ${CROSS_COMPILE}gcc --version | head -1
    echo ""
    echo "Kernel Source: ${KERNEL_SRC}"
    echo "Kernel Build:  ${KERNEL_BUILD}"
    echo ""
    echo "Driver Directories:"
    echo "  Button: ${BUTTON_DIR}"
    echo "  TMP117: ${TMP117_DIR}"
    echo "  ADC:    ${ADC_DIR}"
    echo ""
    echo "BBB Target: ${BBB_USER}@${BBB_IP}"
}

# ============================================================================
# MAIN
# ============================================================================

# Check prerequisites
check_toolchain
check_kernel_build

case "${1:-help}" in
    button)
        build_button
        ;;
    tmp117)
        build_tmp117
        ;;
    adc)
        build_adc
        ;;
    all)
        build_all
        ;;
    clean)
        clean_all
        ;;
    deploy-button)
        deploy_driver "button" "${BUTTON_DIR}"
        ;;
    deploy-tmp117)
        deploy_driver "tmp117" "${TMP117_DIR}"
        ;;
    deploy-adc)
        deploy_driver "adc" "${ADC_DIR}"
        ;;
    button-deploy)
        build_button
        echo ""
        deploy_driver "button" "${BUTTON_DIR}"
        ;;
    tmp117-deploy)
        build_tmp117
        echo ""
        deploy_driver "tmp117" "${TMP117_DIR}"
        ;;
    adc-deploy)
        build_adc
        echo ""
        deploy_driver "adc" "${ADC_DIR}"
        ;;
    info)
        show_info
        ;;
    help|--help|-h)
        show_usage
        ;;
    *)
        log_error "Unknown command: $1"
        echo ""
        show_usage
        exit 1
        ;;
esac


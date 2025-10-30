#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILDER_SCRIPT="$SCRIPT_DIR/linux_builder.sh"

declare -A ARCH_CONFIGS=(
    ["riscv64"]="riscv64-linux-gnu riscv64"
    ["x86"]="i686-linux-gnu x86"
    ["x86_64"]="x86_64-linux-gnu x86_64"
    ["aarch64"]="aarch64-linux-gnu aarch64"
    ["armv7"]="arm-linux-gnueabihf armv7l"
)

setup_arch_env() {
    local arch="$1"
    local config="${ARCH_CONFIGS[$arch]}"
    if [ -z "$config" ]; then
        echo "Error: Unsupported architecture '$arch'"
        echo "Supported: ${!ARCH_CONFIGS[@]}"
        exit 1
    fi

    local triple=$(echo $config | cut -d' ' -f1)
    local cpu=$(echo $config | cut -d' ' -f2)
    
    export ARCH="$cpu"
    export SYSTEM="$arch"
    export HOST="$triple"
    
    if [[ "$arch" = "bruh" ]]; then
        export CC="gcc"
        export CXX="g++"
        export AR="ar"
        export RANLIB="ranlib"
        export STRIP="strip"
        export NM="nm"
        export STRINGS="strings"
        export OBJDUMP="objdump"
        export OBJCOPY="objcopy"
    else
        export CC="${triple}-gcc"
        export CXX="${triple}-g++"
        export AR="${triple}-ar"
        export RANLIB="${triple}-ranlib"
        export STRIP="${triple}-strip"
        export NM="${triple}-nm"
        export STRINGS="${triple}-strings"
        export OBJDUMP="${triple}-objdump"
        export OBJCOPY="${triple}-objcopy"
        export AS="${triple}-as"
    fi
    
    case "$arch" in
        riscv64)
            export CFLAGS="-march=rv64gc -mabi=lp64d -O2"
            export CXXFLAGS="-march=rv64gc -mabi=lp64d -O2"
            ;;
        x86)
            export CFLAGS="-m32 -march=i686 -O2"
            export CXXFLAGS="-m32 -march=i686 -O2"
            export LDFLAGS="-m32"
            ;;
        x64)
            export CFLAGS="-m64 -march=x86-64 -O2"
            export CXXFLAGS="-m64 -march=x86-64 -O2"
            export LDFLAGS="-m64"
            ;;
        aarch64)
            export CFLAGS="-march=armv8-a -O2"
            export CXXFLAGS="-march=armv8-a -O2"
            ;;
        armv7)
            export CFLAGS="-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -O2"
            export CXXFLAGS="-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -O2"
            ;;
    esac
    
    export CFLAGS="${CFLAGS} -fPIC -ffunction-sections -fdata-sections"
    export CXXFLAGS="${CXXFLAGS} -fPIC -ffunction-sections -fdata-sections"
    export LDFLAGS="${LDFLAGS} -Wl,--gc-sections"
}

build_for_arch() {
    local arch="$1"
    echo "=========================================="
    echo "Building for architecture: $arch"
    echo "=========================================="
    
    setup_arch_env "$arch"
    source "$BUILDER_SCRIPT"
    
    echo "✓ Completed build for $arch"
    echo ""
}

# --- main ---
if [ $# -lt 1 ]; then
    echo "Usage: $0 <arch>"
    echo "Available architectures: ${!ARCH_CONFIGS[@]}"
    exit 1
fi

build_for_arch "$1"

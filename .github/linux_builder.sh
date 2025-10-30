#!/bin/bash
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd ${ROOT_DIR}/../ && pwd)"
BUILD_DIR="${ROOT_DIR}/build/${SYSTEM}"
PREFIX="$BUILD_DIR/prefix"
HOST_BUILD_DIR="${ROOT_DIR}/build/host" 
HOST_PREFIX="$HOST_BUILD_DIR/prefix"

mkdir -p "$PREFIX"/{lib,lib64}/{,pkgconfig}
mkdir -p "$HOST_PREFIX"/{bin,lib,lib64}/{,pkgconfig}

export ARCH SYSTEM CC CXX HOST AR RANLIB STRIP NM STRINGS OBJDUMP OBJCOPY
export CFLAGS="-I${PREFIX}/include ${CFLAGS}"
export CPPFLAGS="-I${PREFIX}/include ${CPPFLAGS}"
export LDFLAGS="-L${PREFIX}/lib -L${PREFIX}/lib64 ${LDFLAGS}"
unset PKG_CONFIG_PATH
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig:$PKG_CONFIG_PATH"
export PKG_CONFIG_ALLOW_CROSS=1

cmake_build() {
    local project_name="$1"
    local build_dir="$2"
    shift 2
    echo "[+] Building $project_name for $ARCH..."
    cd "$build_dir" && rm -rf build && mkdir build && cd build
    cat >"$BUILD_DIR/cmake-toolchain-${project_name}.txt" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR ${ARCH})
set(CMAKE_CROSSCOMPILING ON)
set(CMAKE_C_COMPILER $(which ${CC}))
set(CMAKE_CXX_COMPILER $(which ${CXX}))
set(CMAKE_AR $(which ${AR}))
set(CMAKE_RANLIB $(which ${RANLIB}))
set(CMAKE_STRIP $(which ${STRIP}))
set(CMAKE_C_FLAGS_INIT "${CFLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${CXXFLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${LDFLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${LDFLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${LDFLAGS}")
set(CMAKE_INSTALL_PREFIX ${PREFIX})
set(CMAKE_FIND_ROOT_PATH ${PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_BUILD_TYPE Release)
EOF
    
    cmake .. -G Ninja -DCMAKE_TOOLCHAIN_FILE="$BUILD_DIR/cmake-toolchain-${project_name}.txt" "$@"
    ninja -j$(nproc) && ninja install
    echo "✓ $project_name built successfully"
}
cmake_build_host() {
    local project_name="$1"
    local build_dir="$2"
    shift 2
    echo "[+] Building $project_name for HOST..."
    cd "$build_dir" && rm -rf build-host && mkdir build-host && cd build-host
    
    # don't vars leak in
    (
        unset CC CXX AR RANLIB STRIP CFLAGS CXXFLAGS LDFLAGS
        
        cmake .. -G Ninja \
            -DCMAKE_INSTALL_PREFIX="$HOST_PREFIX" \
            -DCMAKE_BUILD_TYPE=Release \
            "$@"
        
        ninja -j$(nproc) && ninja install
    )
    
    echo "✓ $project_name (host) built successfully"
}

meson_build() {
    cd "$2" && rm -rf build && mkdir build
    
    cat >"$BUILD_DIR/meson-cross.txt" <<EOF
[binaries]
c = '$(which $CC)'
cpp = '$(which $CXX)'
ar = '$(which $AR)'
nm = '$(which $NM)'
strip = '$(which $STRIP)'
ranlib = '$(which $RANLIB)'
pkg-config = 'pkg-config'


[built-in options]
c_args = [$(echo "$CFLAGS" | xargs -n1 | sed "s/.*/'&'/" | paste -sd,)]
cpp_args = [$(echo "$CXXFLAGS" | xargs -n1 | sed "s/.*/'&'/" | paste -sd,)]
c_link_args = [$(echo "$LDFLAGS" | xargs -n1 | sed "s/.*/'&'/" | paste -sd,)]
cpp_link_args = [$(echo "$LDFLAGS" | xargs -n1 | sed "s/.*/'&'/" | paste -sd,)]

[host_machine]
system = 'linux'
cpu_family = '${ARCH}'
cpu = '${ARCH}'
endian = 'little'

[properties]
sys_root = '${PREFIX}'
pkg_config_libdir = ['${PREFIX}/lib/pkgconfig', '${PREFIX}/lib64/pkgconfig']
pkg_config_path = ['${PREFIX}/lib/pkgconfig', '${PREFIX}/lib64/pkgconfig']
EOF
    
    shift 2
    meson setup build . --cross-file="$BUILD_DIR/meson-cross.txt" --prefix="$PREFIX" --buildtype=release --default-library=static "$@"
    ninja -C build -j$(nproc) && ninja -C build install
    echo "✔ $1 built successfully"
}

clone_if_missing() {
    [ -d "$2" ] || git clone --depth 1 "$1" "$2"
}

clone_if_missing "https://github.com/tukaani-project/xz.git" "$BUILD_DIR/xz"
clone_if_missing "https://github.com/facebook/zstd.git" "$BUILD_DIR/zstd"
clone_if_missing "https://gitlab.com/federicomenaquintero/bzip2.git" "$BUILD_DIR/bzip2"
clone_if_missing "https://github.com/protocolbuffers/protobuf.git" "$BUILD_DIR/protobuf"
clone_if_missing "https://github.com/KaluaBilla/payload-dumper-ungo.git" "$BUILD_DIR/payload-dumper-ungo"

# build protoc for HOST
build_protobuf_host() {
    echo "[+] Building protoc for host system..."
    cmake_build_host "protobuf-host" "$BUILD_DIR/protobuf" \
        -Dprotobuf_BUILD_TESTS=OFF \
        -Dprotobuf_ABSL_PROVIDER=package \
        -DBUILD_SHARED_LIBS=OFF \
        -Dprotobuf_BUILD_PROTOC_BINARIES=ON
}

# generate .pb.cc and .pb.h using host protoc
generate_protobuf_code() {
    echo "[+] Generating protobuf code for payload-dumper-ungo..."
    local proto_dir="$BUILD_DIR/payload-dumper-ungo/proto"
    
    if [ ! -f "$HOST_PREFIX/bin/protoc" ]; then
        echo "Error: protoc not found at $HOST_PREFIX/bin/protoc"
        return 1
    fi
    
    cd "$proto_dir"
    "$HOST_PREFIX/bin/protoc" --cpp_out=. update_metadata.proto
    
    echo "✓ Protobuf code generated successfully"
}

build_protobuf_target() {
    echo "[+] Building protobuf library for $ARCH..."
    cmake_build "protobuf" "$BUILD_DIR/protobuf" \
        -Dprotobuf_BUILD_TESTS=OFF \
        -Dprotobuf_ABSL_PROVIDER=package \
        -DBUILD_SHARED_LIBS=OFF \
        -Dprotobuf_BUILD_PROTOC_BINARIES=OFF
}

# Build order
cmake_build "liblzma" "$BUILD_DIR/xz" -DBUILD_NLS=OFF
meson_build "zstd" "$BUILD_DIR/zstd/build/meson" -Dbin_programs=false -Dbin_tests=false -Dbin_contrib=false -Dzlib=disabled -Dlzma=disabled -Dlz4=disabled
meson_build "bzip2" "$BUILD_DIR/bzip2" -Ddocs=disabled

build_protobuf_host         
generate_protobuf_code       
build_protobuf_target      
meson_build "payload_dumper_ungo" "$BUILD_DIR/payload-dumper-ungo" -Denable_zip=true -Denable_http=false -Dbuild_static=true

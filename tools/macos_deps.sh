#!/bin/sh
# Builds the libraries a macOS release loads, for an older macOS than this one: tools/macos_deps.sh <prefix> <macOS>
#
# Homebrew's libraries are built for the macOS they were installed on (a build on macOS 26 loads only on 26 and newer),
# so a release made from them would not start on an older Mac. This builds SDL3, the Vulkan loader, shaderc and a small
# ffmpeg (the demuxers and decoders the game's movies and movie mods use: no encoders, LGPL only) from their sources
# into <prefix>, with <macOS> as the oldest macOS they run on. MoltenVK and the Vulkan headers still come from Homebrew
# (its MoltenVK runs on macOS 12 and newer). `make release` uses <prefix> (Makefile: DEPS).
#
# Needs Xcode's command line tools, git, and Homebrew's cmake, pkg-config and vulkan-headers.
# Each library is built once; delete <prefix> to build them again.
set -eu

SDL_VERSION=3.4.18
FFMPEG_VERSION=9.0.2
VULKAN_VERSION=1.4.363
SHADERC_VERSION=2026.4

[ $# -eq 2 ] || { sed -n '2p' "$0"; exit 1; }
mkdir -p "$1"
PREFIX=$(cd "$1" && pwd)
MIN=$2
SRC="$PREFIX/src"
JOBS=$(sysctl -n hw.ncpu)
BREW=$(brew --prefix 2>/dev/null || echo /opt/homebrew)
export MACOSX_DEPLOYMENT_TARGET="$MIN"
CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_OSX_DEPLOYMENT_TARGET=$MIN \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_INSTALL_NAME_DIR=@rpath -DCMAKE_INSTALL_LIBDIR=lib"
mkdir -p "$SRC"

# the stamp holds the macOS it was built for: another target builds it again
built() { [ "$(cat "$PREFIX/.$1" 2>/dev/null)" = "$MIN" ]; }
done_() { echo "$MIN" > "$PREFIX/.$1"; echo "macos_deps: $1 done"; }
fetch() {  # url, folder in $SRC
    [ -d "$SRC/$2" ] && return
    curl -fL --retry 3 -o "$SRC/$2.tar" "$1"
    mkdir -p "$SRC/$2" && tar -xf "$SRC/$2.tar" -C "$SRC/$2" --strip-components 1 && rm "$SRC/$2.tar"
}
cmake_build() {  # source folder, extra arguments
    src=$1; shift
    rm -rf "$src/_build"
    # shellcheck disable=SC2086
    cmake -S "$src" -B "$src/_build" $CMAKE_ARGS "$@" > "$src/_build.log" 2>&1 || { tail -30 "$src/_build.log"; exit 1; }
    cmake --build "$src/_build" -j "$JOBS" >> "$src/_build.log" 2>&1 || { tail -30 "$src/_build.log"; exit 1; }
    cmake --install "$src/_build" >> "$src/_build.log" 2>&1
}

if ! built sdl3; then
    fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-$SDL_VERSION.tar.gz" sdl3
    cmake_build "$SRC/sdl3" -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
    done_ sdl3
fi

if ! built vulkan-loader; then
    fetch "https://github.com/KhronosGroup/Vulkan-Loader/archive/refs/tags/v$VULKAN_VERSION.tar.gz" vulkan-loader
    # (SYSCONFDIR: the system folder it searches for drivers and layers, compiled in: the usual one, not <prefix>/etc)
    cmake_build "$SRC/vulkan-loader" -DVULKAN_HEADERS_INSTALL_DIR="$BREW/opt/vulkan-headers" -DBUILD_TESTS=OFF \
        -DAPPLE_STATIC_LOADER=OFF -DSYSCONFDIR=/usr/local/etc
    done_ vulkan-loader
fi

if ! built shaderc; then
    if [ ! -d "$SRC/shaderc" ]; then
        git clone -q --depth 1 -b "v$SHADERC_VERSION" https://github.com/google/shaderc "$SRC/shaderc"
        (cd "$SRC/shaderc" && python3 utils/git-sync-deps)  # glslang, SPIRV-Tools, SPIRV-Headers at shaderc's commits
    fi
    cmake_build "$SRC/shaderc" -DSHADERC_SKIP_TESTS=ON -DSHADERC_SKIP_EXAMPLES=ON -DSHADERC_SKIP_COPYRIGHT_CHECK=ON \
        -DSPIRV_SKIP_TESTS=ON -DSPIRV_SKIP_EXECUTABLES=ON -DSPIRV_WERROR=OFF -DENABLE_GLSLANG_BINARIES=OFF \
        -DSKIP_GLSLANG_INSTALL=ON -DSKIP_SPIRV_TOOLS_INSTALL=ON -DSPIRV_TOOLS_BUILD_STATIC=ON
    done_ shaderc
fi

if ! built ffmpeg; then
    fetch "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz" ffmpeg
    # runtime/hle/movie.c: mp4 (H.264 + AAC, the game's movies) and WebM / Matroska (VP8 / VP9, Vorbis / Opus: mods)
    # (prefix /, installed with DESTDIR: the libraries keep their configure line, which then names no folder here)
    (cd "$SRC/ffmpeg" && make distclean > /dev/null 2>&1 || true
     ./configure --prefix=/ --enable-shared --disable-static --install-name-dir=@rpath \
        --extra-cflags="-mmacosx-version-min=$MIN" --extra-ldflags="-mmacosx-version-min=$MIN" \
        --disable-programs --disable-doc --disable-network --disable-autodetect --disable-everything \
        --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample --enable-zlib \
        --enable-protocol=file --enable-demuxer=mov,matroska \
        --enable-decoder=h264,aac,vp8,vp9,vorbis,opus --enable-parser=h264,aac,vp8,vp9,vorbis,opus \
        > "$SRC/ffmpeg.log" 2>&1 || { tail -30 "$SRC/ffmpeg.log"; exit 1; }
     make -j "$JOBS" >> "$SRC/ffmpeg.log" 2>&1 || { tail -30 "$SRC/ffmpeg.log"; exit 1; }
     make install DESTDIR="$PREFIX" >> "$SRC/ffmpeg.log" 2>&1)
    done_ ffmpeg
fi

echo "macos_deps: libraries for macOS $MIN in $PREFIX"

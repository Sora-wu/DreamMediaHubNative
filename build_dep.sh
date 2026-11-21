#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ffmpeg_source="$script_dir/third/src/ffmpeg"
platform=linux
jobs="$(getconf _NPROCESSORS_ONLN)"
if [[ "$(uname -s)" == Darwin ]]; then
    platform=macos
    if command -v brew >/dev/null 2>&1; then
        brew_prefix="$(brew --prefix)"
        export PKG_CONFIG_PATH="$brew_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    fi
fi

cd "$ffmpeg_source"
if [[ -f ffbuild/config.mak ]]; then
    make clean
fi
./configure --prefix="$script_dir/third/build/$platform" --enable-shared --disable-programs --disable-doc --enable-libx264 --enable-gpl \
  --enable-libfdk-aac --enable-nonfree --enable-libx265
make -j"$jobs"
make install

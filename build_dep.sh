#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ffmpeg_source="$script_dir/third/src/ffmpeg"

cd "$ffmpeg_source"
if [[ -f ffbuild/config.mak ]]; then
    make clean
fi
./configure --prefix="$script_dir/third/build/linux" --enable-shared --disable-programs --enable-libx264 --enable-gpl \
  --enable-libfdk-aac --enable-nonfree --enable-libx265
make -j
make install

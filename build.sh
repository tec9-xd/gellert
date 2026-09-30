#!/bin/bash
set -e
cd "$(dirname "$0")"

if [ ! -f libs/funchook/build/libfunchook.a ]; then
    rm -rf libs
    mkdir -p libs
    git clone --depth 1 https://github.com/Doctor-Coomer/funchook.git libs/funchook
    cmake -S libs/funchook -B libs/funchook/build -DCMAKE_BUILD_TYPE=Release
    cmake --build libs/funchook/build -j"$(nproc)"
fi

make -j"$(nproc)"
echo "built $(pwd)/cs2.so"

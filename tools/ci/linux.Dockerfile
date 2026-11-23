# Linux build environment: Ubuntu 22.04 (glibc 2.35), so the game runs on
# distributions from 2022 on; GCC 12 (C++20) and a current CMake from pip.
# Used by the Linux job in .github/workflows/build.yml. Local use, from the
# repo root:
#   docker build -t d2k-linux -f tools/ci/linux.Dockerfile tools/ci
#   docker run --rm -v "$PWD:/src:ro" -v d2k-linux-build:/build d2k-linux
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-12 g++-12 make ninja-build git ca-certificates pkg-config python3-pip \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev \
        libxkbcommon-dev libwayland-dev wayland-protocols libegl1-mesa-dev libgl1-mesa-dev \
        libgles2-mesa-dev libdrm-dev libgbm-dev libasound2-dev libpulse-dev libdbus-1-dev libudev-dev \
        libpipewire-0.3-dev libdecor-0-dev \
    && rm -rf /var/lib/apt/lists/* \
    && pip3 install --no-cache-dir "cmake==3.31.6"
ENV CC=gcc-12 CXX=g++-12

# Configure, build, test and package Release into /build (a volume keeps it
# between runs); the package lands in /build/packages.
CMD ["sh", "-c", "cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build /build && cd /build && SDL_AUDIO_DRIVER=dummy SDL_VIDEO_DRIVER=dummy ./bin/d2k_tests && cpack -G TGZ"]

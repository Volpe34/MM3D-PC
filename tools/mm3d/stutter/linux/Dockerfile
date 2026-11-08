FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential clang lld cmake ninja-build git python3 pkg-config curl ca-certificates xz-utils \
      libsdl2-dev libglew-dev libgl-dev libopengl-dev libegl-dev nlohmann-json3-dev libspdlog-dev libfmt-dev \
      libzip-dev libbz2-dev zlib1g-dev libzstd-dev liblzma-dev libpng-dev libtinyxml2-dev \
      libvulkan-dev glslang-tools glslc spirv-tools libshaderc-dev \
      libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev libwayland-dev wayland-protocols libxkbcommon-dev \
      libogg-dev libopus-dev libopusfile-dev libvorbis-dev zipcmp zipmerge ziptool \
 && rm -rf /var/lib/apt/lists/*
RUN curl -fsSL https://github.com/Kitware/CMake/releases/download/v3.31.11/cmake-3.31.11-linux-x86_64.tar.gz \n      | tar -xz -C /opt && ln -sf /opt/cmake-3.31.11-linux-x86_64/bin/* /usr/local/bin/ && cmake --version
RUN mkdir -p /opt/dxc && curl -fsSL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/linux_dxc_2026_09_28.x86_x64.tar.gz \
      | tar -xz -C /opt/dxc && ls /opt/dxc /opt/dxc/bin && /opt/dxc/bin/dxc --version

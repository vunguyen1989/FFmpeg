# Base image
FROM ubuntu:22.04

# Install build tools and dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    yasm \
    nasm \
    pkg-config \
    libtool \
    autoconf \
    automake \
    cmake \
    git \
    curl \
    python3 \
    python3-pip \
    clang \
    zlib1g-dev \
    gdb \
    lldb \
    valgrind \
    strace \
    ltrace \
    vim \
    libbz2-dev \
    && apt-get clean


WORKDIR /workspace

RUN git clone --depth 1 https://github.com/mstorsjo/fdk-aac.git && \
    cd fdk-aac && \
    autoreconf -fiv && \
    ./configure --disable-shared --enable-static CFLAGS="-g -O0" && \
    make -j$(nproc) && make install

RUN git clone --depth 1 https://code.videolan.org/videolan/x264.git && \
    cd x264 && \
    ./configure --enable-static --disable-shared --enable-pic --enable-debug CFLAGS="-g -O0" && \
    make -j$(nproc) && make install

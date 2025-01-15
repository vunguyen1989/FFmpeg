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
    libx264-dev \
    libx265-dev \
    libvpx-dev \
    libopus-dev \
    libfdk-aac-dev \
    zlib1g-dev \
    && apt-get clean

# Set working directory
WORKDIR /workspace

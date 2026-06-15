## Building & Compilation

### Dependencies

- **FFmpeg** (libavcodec, libavformat, libavutil, libswscale, libswresample)
- **SDL2** or **SDL 1.2**
- **CMake** 3.11+
- **GCC**


### Build FFmpeg Static Libraries từ Source

Để build FFmpeg static libraries (lib*.a) trực tiếp từ source và debug với VS Code:

```bash
make distclean  # bắt buộc trước khi configure lại

# 1. Configure với static build
./configure \
    --pkg-config-flags="--static" \
    --extra-cflags="-g -O0 -I/opt/homebrew/include" \
    --extra-ldflags="-L/opt/homebrew/lib \
        -framework VideoToolbox \
        -framework CoreFoundation \
        -framework CoreMedia \
        -framework CoreVideo \
        -framework CoreServices \
        -framework Security \
        -framework AudioToolbox \
        -framework CoreAudio" \
    --extra-libs="-lpthread -lm -liconv -llzma" \
    --disable-stripping \
    --disable-optimizations \
    --enable-static \
    --disable-shared \
    --enable-debug \
    --enable-gpl \
    --enable-nonfree


# 2. Build static libraries (không cần make install)
make -j$(sysctl -n hw.logicalcpu)
```

**Lưu ý:**
- Trên **Mac Intel**: dùng `/usr/local` thay vì `/opt/homebrew`
- Trên **Mac M1/M2/M3**: dùng `/opt/homebrew`
- Static libs sẽ nằm tại: `libavcodec/libavcodec.a`, `libavformat/libavformat.a`, `libavutil/libavutil.a`, `libswscale/libswscale.a`, `libswresample/libswresample.a`

**Link với Video Player:**

```bash
# Từ thư mục doc/examples/videoplayer
gcc -Wall -Wextra -Wno-deprecated-declarations -g -O0 \
    -I../../libavutil -I../../libavcodec -I../../libavformat -I../../libswscale -I../../libswresample \
    -I$(brew --prefix)/include/SDL2 \
    -o player app.c \
    ../../libavutil/libavutil.a \
    ../../libavformat/libavformat.a \
    ../../libavcodec/libavcodec.a \
    ../../libswscale/libswscale.a \
    ../../libswresample/libswresample.a \
    -lm -lz -lpthread \
    -L$(brew --prefix)/lib -lSDL2
```

### Build trên macOS (với Homebrew)

```bash
# Cài đặt dependencies
brew install sdl2

# Build với Makefile
cd lab
make build

# Run
./player.app ../Iron_Man-Trailer_HD.mp4 100
```
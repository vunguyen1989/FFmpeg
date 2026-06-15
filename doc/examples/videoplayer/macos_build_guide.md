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
- Trên **Mac M1/M2/M3**: dùng `/opt/homebrew`
- Static libs sẽ nằm tại: `libavcodec/libavcodec.a`, `libavformat/libavformat.a`, `libavutil/libavutil.a`, `libswscale/libswscale.a`, `libswresample/libswresample.a`

**Link với Video Player:**

### Build trên macOS (với Homebrew)

```bash
# Cài đặt dependencies
brew install sdl2

# Trong videoplayer, uild với Makefile
make build

```
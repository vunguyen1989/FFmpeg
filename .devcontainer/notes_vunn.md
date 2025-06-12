# 1 confige
```
./configure \
  --prefix=/usr/local/ffmpeg \
  --extra-cflags="-g -O0" \
  --extra-ldflags="-g" \
  --disable-optimizations \
  --enable-debug \
  --enable-static \
  --disable-shared \
  --enable-gpl \
  --enable-nonfree \
  --enable-libx264 \
  --enable-libx265 \
  --enable-libvpx \
  --enable-libopus \
  --enable-libfdk-aac \
  --disable-doc
```

# 2 install

```
  make -j$(nproc)   
```
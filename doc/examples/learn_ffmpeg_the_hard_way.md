## ✅ Mục tiêu:
- **Build static libraries** (`libavcodec.a`, `libavformat.a`, v.v.)
- **Bật debug symbols** (`-g`)
- **Tắt tối ưu hóa** (`-O0` để dễ debug)
- **Không cần shared libraries** (`.so`)

---

## ✅ Cấu hình đề xuất:

```bash
cd /workspace

make distclean  # reset sạch nếu đã từng build

./configure \
  --prefix=/usr/local \
  --disable-shared \
  --enable-static \
  --disable-optimizations \
  --enable-debug \
  --extra-cflags="-g -O0" \
  --extra-ldflags="-g"
```

### 🔍 Giải thích:
- `--disable-shared`: không build `.so`
- `--enable-static`: build `.a`
- `--disable-optimizations`: tắt tối ưu để debug dễ hơn
- `--enable-debug`: thêm thông tin debug
- `--extra-cflags="-g -O0"`: thêm `-g` và tắt tối ưu bằng `-O0`
- `--extra-ldflags="-g"`: đảm bảo thông tin debug có mặt ở các binary đã link

---

## ✅ Build và cài đặt:

```bash
make -j$(nproc)
make install
```

---

## ✅ Kiểm tra:
Sau khi cài đặt, bạn có thể kiểm tra thử `.a` files:

```bash
ls /usr/local/lib | grep '\.a'
```

Và xem thử có debug symbols không:

```bash
file /usr/local/lib/libavcodec.a
```

Hoặc dùng `nm` hoặc `readelf` để kiểm tra symbol table.

---


## build remux
```
gcc -g -I/usr/local/include 2_remuxing.c \
/usr/local/lib/libavformat.a \
/usr/local/lib/libavcodec.a \
/usr/local/lib/libavdevice.a \
/usr/local/lib/libavfilter.a \
/usr/local/lib/libswresample.a \
/usr/local/lib/libswscale.a \
/usr/local/lib/libavutil.a \
-o remux -lpthread -lz -lbz2 -lm
```

## build transcode
```
gcc -g -I/usr/local/include 3_transcoding.c video_debugging.c \
/usr/local/lib/libavformat.a \
/usr/local/lib/libavcodec.a \
/usr/local/lib/libavdevice.a \
/usr/local/lib/libavfilter.a \
/usr/local/lib/libswresample.a \
/usr/local/lib/libswscale.a \
/usr/local/lib/libavutil.a \
-o 3_transcoding -lpthread -lz -lbz2 -lm
```
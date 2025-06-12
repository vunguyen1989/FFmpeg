#!/bin/bash

# Script này dùng để cấu hình và biên dịch FFmpeg với các tùy chọn cụ thể.
# Nó bao gồm các cờ cho pkg-config, cờ biên dịch và liên kết bổ sung,
# cũng như các tùy chọn để bật/tắt các thư viện và tính năng.

echo "Bắt đầu cấu hình FFmpeg..."

./configure \
    --pkg-config-flags="--static" \
    --extra-cflags="-g -O0 -I/usr/local/include" \
    --extra-ldflags="-L/usr/local/lib" \
    --extra-libs="-lpthread -lm" \
    --disable-stripping \
    --disable-optimizations \
    --enable-libx264 \
    --enable-libfdk-aac \
    --disable-shared \
    --enable-static \
    --enable-debug \
    --enable-gpl \
    --enable-nonfree \
    --enable-postproc \
    --enable-decoder=rawvideo \
    --enable-filter=pp,spp

# Kiểm tra mã thoát của lệnh configure
if [ $? -ne 0 ]; then
    echo "Lỗi: Lệnh configure thất bại."
    exit 1
fi

echo "Cấu hình hoàn tất. Bắt đầu biên dịch FFmpeg..."

# Biên dịch FFmpeg sử dụng tất cả các lõi CPU có sẵn
make -j$(nproc)

# Kiểm tra mã thoát của lệnh make
if [ $? -ne 0 ]; then
    echo "Lỗi: Lệnh make thất bại."
    exit 1
fi

echo "Biên dịch hoàn tất. Bắt đầu cài đặt FFmpeg..."

# Cài đặt FFmpeg
make install

# Kiểm tra mã thoát của lệnh make install
if [ $? -ne 0 ]; then
    echo "Lỗi: Lệnh make install thất bại."
    exit 1
fi

echo "Cài đặt FFmpeg hoàn tất!"

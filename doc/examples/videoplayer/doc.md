# FFmpeg Video Player Documentation

## Table of Contents

1. [Overview](#overview)
2. [Core Concepts](#core-concepts)
3. [Technology Stack](#technology-stack)
4. [Tutorial Structure](#tutorial-structure)
5. [Detailed Tutorial Breakdown](#detailed-tutorial-breakdown)
6. [Architecture & Flow](#architecture--flow)
7. [Key Algorithms](#key-algorithms)
8. [Data Structures](#data-structures)
9. [Building & Compilation](#building--compilation)

---

## Overview

Repo này là một tutorial hướng dẫn chi tiết cách xây dựng một video player từ đầu sử dụng **FFmpeg** và **SDL** (Simple DirectMedia Layer). Dựa trên tutorial gốc của Martin Böhme và được cập nhật, review lại bởi Rambod Rahmani.

Mục tiêu: Xây dựng một video player hoàn chỉnh trong **ít hơn 1000 dòng code** C.

Link gốc: http://dranger.com/ffmpeg/

---

## Core Concepts

### 1. Container (Format)

Container (còn gọi là wrapper format) là file format chứa dữ liệu video/audio. Container xác định cách thông tin được lưu trữ trong file.

**Ví dụ các container phổ biến:**
- AVI (Audio Video Interleave)
- MP4
- Matroska (.mkv)
- WebM
- QuickTime (.mov)

### 2. Stream

Mỗi container có thể chứa nhiều **stream** (luồng) khác nhau:
- Video stream
- Audio stream
- Subtitle stream
- ...

### 3. Codec

**Codec** (COder/DECoder) định nghĩa cách dữ liệu được mã hóa và giải mã.

**Video Codecs phổ biến:**
- H.264/AVC
- H.265/HEVC
- VP9
- AV1
- DivX
- XviD

**Audio Codecs phổ biến:**
- MP3
- AAC
- Opus
- FLAC

### 4. Packet vs Frame

| Khái niệm | Mô tả |
|-----------|-------|
| **Packet** | Đơn vị dữ liệu được đọc từ stream, chứa dữ liệu nén đã được mã hóa |
| **Frame** | Dữ liệu sau khi giải mã (decompressed), là hình ảnh hoặc mẫu âm thanh thực tế |

Một packet có thể chứa một hoặc nhiều frame (đặc biệt với audio).

### 5. YUV/YCbCr Color Space

- **Y**: Luminance (độ sáng/luma)
- **U/V (Cb/Cr)**: Chrominance (màu sắc)

**YUV420P**: Format phổ biến nhất, subsample 4:2:0 - 1 mẫu màu cho mỗi 4 mẫu sáng, giúp tiết kiệm bandwidth vì mắt người nhạy cảm với độ sáng hơn màu sắc.

#### Quy ước mẫu màu: 4:2:0

Con số **4:2:0** đại diện cho tỷ lệ lấy mẫu (subsampling ratio) trong một **ma trận điểm ảnh kích thước $4 \times 2$** (tức là 2 hàng, mỗi hàng 4 pixel, tổng cộng 8 pixel).

Cách đặt tên này hơi thiếu trực quan một chút, nhưng bạn có thể hiểu ý nghĩa của từng số dựa trên sơ đồ dịch chuyển của dữ liệu màu sắc như sau:

---

### Ý nghĩa của từng con số: $J : A : B$

* **Số 4 (Số đầu tiên - $J$):** Đại diện cho **chiều rộng của ma trận mẫu**, luôn là 4 pixel. Đây là hằng số tham chiếu, đại diện cho việc cả 4 pixel trong hàng đều có đủ 4 mẫu sáng ($Y$).
* **Số 2 (Số thứ hai - $A$):** Số mẫu màu (Chroma - $U/V$) ở **hàng đầu tiên**. Số 2 có nghĩa là trong 4 pixel của hàng đầu tiên, chỉ có 2 pixel được lấy mẫu màu (2 pixel còn lại sẽ "xài ké" màu của pixel bên cạnh).
* **Số 0 (Số thứ ba - $B$):** Số mẫu màu (Chroma - $U/V$) ở **hàng thứ hai**. Số 0 có nghĩa là hàng thứ hai **không được lấy một mẫu màu mới nào cả**, nó sẽ bê nguyên xi dữ liệu màu của hàng thứ nhất xuống để dùng chung.

---

### Trực quan hóa ma trận $4 \times 2$ (8 Pixel)

Để dễ hình dung cách phân bổ dữ liệu màu, hãy nhìn vào cách sắp xếp của các pixel ở hai hàng:

* **Hàng 1:** `[Màu 1]` `[Xài ké Màu 1]` `[Màu 2]` `[Xài ké Màu 2]` $\rightarrow$ (Có **2** mẫu màu)
* **Hàng 2:** `[Ké Hàng 1]` `[Ké Hàng 1]` `[Ké Hàng 1]` `[Ké Hàng 1]` $\rightarrow$ (Có **0** mẫu màu mới)

### So sánh nhanh với các định dạng khác để bạn thấy rõ sự khác biệt:

* **4:4:4** (Không nén): Hàng một có 4 màu, hàng hai có 4 màu. Mỗi pixel có màu riêng, nét nhất nhưng nặng nhất.
* **4:2:2** (Nén theo chiều ngang): Hàng một có 2 màu, hàng hai có 2 màu. Thường dùng trong truyền hình chất lượng cao.
* **4:2:0** (Nén cả ngang lẫn dọc): Hàng một có 2 màu, hàng hai lấy luôn màu hàng một (0 màu mới). Tiết kiệm dung lượng nhất, phổ biến nhất trên Internet và các file MP4 hiện nay.

---

## Technology Stack

### FFmpeg Libraries

| Library | Chức năng |
|---------|-----------|
| **libavformat** | Demuxing/Muxing - đọc và ghi các container format |
| **libavcodec** | Encoding/Decoding - nén và giải nén audio/video |
| **libavutil** | Tiện ích chung (memory, math, etc.) |
| **libswscale** | Image scaling và conversion |
| **libswresample** | Audio resampling và conversion |

### SDL (Simple DirectMedia Layer)

SDL là cross-platform multimedia library, dùng để:
- Hiển thị video lên màn hình (YUV overlay)
- Output âm thanh
- Xử lý events (keyboard, mouse, etc.)

**Phiên bản:**
- SDL 1.2 (deprecated)
- SDL 2.0 (recommended)

---

## Tutorial Structure

Repo gồm **7 tutorials** tiến dần từ cơ bản đến nâng cao:

```
Tutorial 01: Making Screencaps      → Đọc video, lưu frame ra PPM
Tutorial 02: SDL and Video          → Hiển thị video lên màn hình
Tutorial 03: Playing Sound          → Phát âm thanh
Tutorial 04: Spawning Threads       → Tách thread cho video/audio
Tutorial 05: Synching Video         → Đồng bộ video với audio
Tutorial 06: Synching Audio         → Đồng bộ audio với video
Tutorial 07: Seeking                → Thêm tính năng tua/lùi video

Player (app.c):                     → Video + Audio player (SDL2-based) [Tutorial 01-03]
```

---

## Detailed Tutorial Breakdown

### Tutorial 01: Making Screencaps

**Mục tiêu:** Học cách mở file video và trích xuất các frame dưới dạng hình ảnh.

**Các bước chính:**

```
1. avformat_open_input()      → Mở file
2. avformat_find_stream_info()→ Đọc thông tin stream
3. av_dump_format()           → In thông tin debug
4. Tìm video stream          → Duyệt qua các stream để tìm video
5. Tìm codec                 → Tìm decoder phù hợp
6. avcodec_open2()            → Mở codec
7. av_read_frame()            → Đọc packet từ stream
8. avcodec_send_packet()      → Gửi packet cho decoder
9. avcodec_receive_frame()    → Nhận frame đã giải mã
10. Lưu frame ra PPM         → Ghi file hình ảnh
```

**Output:** Các file PPM chứa ảnh trích xuất từ video.

**Lưu ý quan trọng về Packet → Frame:**
- Một **packet** có thể chứa **nhiều frame** (đặc biệt với video codecs nén cao)
- Cần dùng vòng while để "drain" (xả) hết tất cả frames từ 1 packet:

```c
ret = avcodec_send_packet(pCodecCtx, pPacket);
while (ret >= 0) {
    ret = avcodec_receive_frame(pCodecCtx, pFrame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
    // Xử lý frame...
}
```

**Tại sao 1 packet có thể có nhiều frame?**
- Video codecs dùng inter-frame compression (P-frames, B-frames)
- 1 packet H.264 có thể chứa nhiều slices → nhiều frames
- Decoder cần nhận đủ packet để output frame (đặc biệt B-frames)

### Tutorial 02: SDL and Video

**Mục tiêu:** Hiển thị video trực tiếp lên màn hình sử dụng SDL.

**Khái niệm mới:**
- **YUV Overlay**: SDL cung cấp cách hiển thị YUV data trực tiếp lên màn hình
- **YV12**: Format YUV nhanh nhất
- **SDL_Window**: Cửa sổ hiển thị
- **SDL_Renderer**: Bộ render graphics
- **SDL_Texture**: Texture chứa dữ liệu hình ảnh

**Flow:**
```
Frame (từ decoder) → SwsContext (convert) → pict (YUV420P) → SDL_UpdateYUVTexture() → Texture → Render → Screen
```

**Tại sao cần chuyển đổi pFrame → pict?**

```
pFrame (format từ video codec - NV12, YUV420P, RGB24, etc.)
    ↓ sws_scale()
pict (YUV420P - format cố định cho SDL)
    ↓ SDL_UpdateYUVTexture()
Texture
    ↓ SDL_RenderCopy()
Screen
```

1. **pFrame** - Frame sau khi decode từ video:
   - Format pixel phụ thuộc vào codec của video gốc
   - Có thể là NV12, YUV420P, RGB24, v.v.

2. **pict** - Frame buffer cho SDL hiển thị:
   - Được allocate với format cố định là `YV12` (YUV420P)
   - SDL yêu cầu format này để `SDL_UpdateYUVTexture()` hoạt động

3. **SwsContext** - FFmpeg software scaler:
   - Chuyển đổi pixel format
   - Có thể resize nếu cần
   - Dù cho video đã là YUV420P, vẫn cần copy qua `pict` vì SDL cần buffer riêng để update texture

### Tutorial 03: Playing Sound

**Mục tiêu:** Thêm playback âm thanh cho video player.

**Khái niệm mới:**

- **Sample Rate**: Tốc độ lấy mẫu âm thanh (22,050 Hz, 44,100 Hz, etc.)
- **Channels**: Số kênh âm thanh (mono=1, stereo=2)
- **SDL_AudioSpec**: Cấu hình audio device
- **Audio Callback**: Hàm được gọi khi SDL cần thêm audio data
- **PacketQueue**: Queue để lưu audio packets (decode video/audio chạy song song)
- **SwrContext**: Audio resampling - chuyển đổi audio format

**Audio callback flow:**
```
SDL calls callback → packet_queue_get() → avcodec_send_packet() → avcodec_receive_frame() → swr_convert() → memcpy to buffer → SDL plays audio
```

**Các thành phần đã thêm vào app.c:**

1. **Audio Stream Detection**: Tìm cả video stream lẫn audio stream trong video file

2. **Audio Device**: Mở SDL audio device với specs từ codec:
   ```c
   SDL_OpenAudioDevice(NULL, 0, &wanted_specs, &specs, SDL_AUDIO_ALLOW_FORMAT_CHANGE)
   ```

3. **PacketQueue**: Queue lưu audio packets - main loop put packets vào queue, audio callback lấy ra decode

4. **Audio Callback**: SDL gọi khi cần data để phát âm thanh:
   - Lấy packet từ queue
   - Decode bằng FFmpeg
   - Resample nếu cần (swr_convert)
   - Copy vào SDL buffer

5. **Audio Resampling**: Chuyển đổi audio format từ video (VD: AV_SAMPLE_FMT_FLTP) sang format SDL yêu cầu (AUDIO_S16SYS)

**Flow xử lý trong main loop:**
```
av_read_frame() → lấy packet
  → Video packet: decode ngay, hiển thị frame
  → Audio packet: packet_queue_put(&audioq, pPacket)
                    ↓
              SDL Audio Callback:
                packet_queue_get() → decode → resample → play
```

### Tutorial 04: Spawning Threads

**Mục tiêu:** Tách biệt xử lý video/audio thành các thread riêng biệt để tăng hiệu suất.

**Kiến trúc mới:**

```
                    ┌──────────────┐
                    │   Decode     │
                    │   Thread     │
                    └──────┬───────┘
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
        ┌──────────┐ ┌──────────┐ ┌──────────┐
        │  Audio   │ │  Video   │ │  Event   │
        │  Queue   │ │  Queue   │ │  Loop    │
        └────┬─────┘ └────┬─────┘ └────┬─────┘
             ▼            ▼            ▼
        ┌──────────┐ ┌──────────┐ ┌──────────┐
        │  Audio   │ │  Video   │ │  Screen  │
        │  Thread  │ │  Thread  │ │  Display │
        └──────────┘ └──────────┘ └──────────┘
```

**Lợi ích:**
- Decode không chặn hiển thị
- Audio luôn được phát liên tục
- Video có thể skip frame nếu cần

### Tutorial 05: Synching Video

**Mục tiêu:** Đồng bộ video với audio clock.

**Khái niệm quan trọng:**

**PTS (Presentation Timestamp):** Thời điểm hiển thị frame

**DTS (Decoding Timestamp):** Thời điểm giải mã packet

**I, P, B Frames:**
- **I Frame (Intra)**: Frame đầy đủ, không phụ thuộc frame khác
- **P Frame (Predicted)**: Chỉ chứa sự khác biệt với frame trước
- **B Frame (Bidirectional)**: Phụ thuộc cả frame trước và sau

**Ví dụ:**
```
Display order: I B B P
Storage order: I P B B

PTS:  1 4 2 3
DTS:  1 2 3 4
```

**Thuật toán sync:**
```
1. Lấy PTS của frame hiện tại
2. Tính thời gian chờ = PTS tiếp theo - PTS hiện tại
3. SDL_Delay(thời gian chờ)
4. Hiển thị frame tiếp theo
```

### Tutorial 06: Synching Audio

**Mục tiêu:** Đồng bộ audio với video clock.

**Video Clock:**
```c
// Cập nhật video clock
video_clock = pts + (current_time - pts_time)

// Lấy thời điểm hiện tại của video
get_video_clock() = pts + (av_gettime() - frame_timer)
```

**Audio Sync:**
```c
// Nếu audio clock != video clock:
// - Audio chạy nhanh hơn → bỏ qua samples
// - Audio chạy chậm hơn → thêm samples (padding)
```

### Tutorial 07: Seeking

**Mục tiêu:** Thêm tính năng tua video (seek forward/backward).

**Các phím điều khiển:**
- **Left/Right Arrow**: ±10 seconds
- **Up/Down Arrow**: ±60 seconds

**av_seek_frame():**
```c
av_seek_frame(format_ctx, stream_index, timestamp, flags);
// flags: AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_FRAME
```

**Xử lý seek:**
```
1. Nhận input từ user
2. Set seek flag trong VideoState
3. Decode thread kiểm tra flag
4. Gọi av_seek_frame()
5. Flush các queue
6. Tiếp tục decode từ vị trí mới
```

### Current Player (app.c)

Đây là implementation hiện tại trong file `app.c`, kết hợp các concepts từ Tutorial 01, 02, 03:

**Cấu trúc chính:**
```
1. SDL_Init()                    → Khởi tạo SDL (VIDEO | AUDIO | TIMER)
2. avformat_open_input()         → Mở file video
3. avformat_find_stream_info()   → Đọc thông tin stream
4. Tìm video stream + audio stream → Duyệt tìm các stream
5. Audio: avcodec_find_decoder() → Tìm audio decoder
6. Audio: SDL_OpenAudioDevice()  → Mở audio device
7. Audio: avcodec_open2()        → Mở audio codec
8. Audio: packet_queue_init()    → Khởi tạo audio queue
9. Audio: SDL_PauseAudioDevice() → Bắt đầu phát
10. Video: avcodec_find_decoder()→ Tìm video decoder
11. Video: avcodec_alloc_context3() → Tạo video codec context
12. Video: avcodec_open2()       → Mở video codec
13. SDL_CreateWindow()           → Tạo cửa sổ
14. SDL_CreateRenderer()         → Tạo renderer
15. SDL_CreateTexture()          → Tạo texture (YV12)
16. sws_getContext()             → Tạo video scaler context
17. Vòng lặp chính:
    - av_read_frame()            → Đọc packet
    - Video packet: decode → sws_scale → SDL_UpdateYUVTexture → Render
    - Audio packet: packet_queue_put(&audioq, pPacket)
    - SDL_PollEvent()            → Xử lý quit event
18. Cleanup: av_frame_free(), avcodec_free_context()×2, avformat_close_input(), SDL_Quit()
```

**Các thành phần audio trong app.c:**
- `PacketQueue audioq` - Queue lưu audio packets
- `audio_callback()` - SDL gọi để lấy audio data
- `audio_decode_frame()` - Decode audio từ queue
- `audio_resampling()` - Resample về format SDL (S16)

**Audio flow:**
```
Main Loop: av_read_frame() → put audio packet vào queue
Audio Callback: get packet → decode → resample → play
```

**Các điểm quan trọng:**
- Dùng `avcodec_send_packet()` / `avcodec_receive_frame()` API mới
- Một packet có thể tạo nhiều frame → cần vòng while để drain hết
- SDL yêu cầu texture YV12 (YUV420P)

---

## Architecture & Flow

### Overall Video Player Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Main Event Loop                             │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │  1. Poll events (SDL_PollEvent)                               │ │
│  │  2. Handle quit/keypress                                      │ │
│  │  3. Refresh screen (SDL_RenderPresent)                        │ │
│  │  4. Check if seek requested                                   │ │
│  └────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
                                  │
        ┌─────────────────────────┼─────────────────────────┐
        ▼                         ▼                         ▼
┌───────────────┐       ┌─────────────────┐       ┌───────────────┐
│  Decode       │       │  Audio Thread   │       │ Video Thread  │
│  Thread       │       │                 │       │               │
│               │       │ - Get packet    │       │ - Get packet  │
│ - Read file   │       │ - Decode        │       │ - Decode      │
│ - Find stream │       │ - Resample      │       │ - Scale       │
│ - Read packet │       │ - Put to SDL    │       │ - Put to SDL  │
└───────┬───────┘       └────────┬────────┘       └───────┬───────┘
        │                        │                        │
        └────────────────────────┴────────────────────────┘
                                 │
                                 ▼
                    ┌────────────────────────┐
                    │   Packet Queue         │
                    │   (AVPacket*)          │
                    └───────────┬────────────┘
                                │
        ┌───────────────────────┼───────────────────────┐
        ▼                                               ▼
┌─────────────────────┐                     ┌─────────────────────┐
│   Video Packet Q    │                     │   Audio Packet Q    │
└─────────────────────┘                     └─────────────────────┘
```

### FFmpeg Data Flow

```
Video File (Container)
        │
        ▼
┌───────────────────┐
│  libavformat      │  ← avformat_open_input()
│  (Demuxer)        │  ← avformat_find_stream_info()
└─────────┬─────────┘
          │ AVPacket (compressed data)
          ▼
┌───────────────────┐
│  libavcodec       │  ← avcodec_find_decoder()
│  (Decoder)        │  ← avcodec_open2()
└─────────┬─────────┘
          │ AVFrame (raw video)
          ▼
┌───────────────────┐
│  libswscale       │  ← sws_getContext()
│  (Converter)      │  ← sws_scale()
└─────────┬─────────┘
          │ AVFrame (RGB/YUV)
          ▼
┌───────────────────┐
│  SDL              │  ← SDL_UpdateTexture()
│  (Display)        │  ← SDL_RenderCopy()
└───────────────────┘
          │
          ▼
    [Screen Display]
```

---

## Key Algorithms

### 1. Video Frame Timing

```c
// Tính thời gian delay cho frame tiếp theo
double compute_delay(AVFrame *frame) {
    double pts = av_frame_get_best_effort_timestamp(frame);
    double frame_rate = av_q2d(stream->avg_frame_rate);

    // Thời gian giữa các frame
    double frame_duration = 1.0 / frame_rate;

    // Delay = thời gian frame - thời gian đã trôi
    double delay = frame_duration - (current_time - last_frame_time);

    return delay;
}
```

### 2. Audio Clock

```c
typedef struct {
    double audio_clock;           // Thời điểm hiện tại của audio
    int audio_hw_buf_size;       // Kích thước buffer
    int audio_buf_size;          // Kích thước buffer đã fill
    int audio_buf_index;         // Vị trí hiện tại trong buffer
} AudioState;

double get_audio_clock() {
    double pts = audio_clock;
    // Điều chỉnh với lượng data còn lại trong buffer
    pts -= (double)audio_buf_size / (double)audio_clock;
    return pts;
}
```

### 3. Video Clock

```c
typedef struct {
    double video_clock;          // Thời điểm hiện tại của video
    double frame_last_pts;       // PTS của frame trước
    double frame_last_delay;     // Delay của frame trước
    double frame_timer;          // Thời điểm bắt đầu hiển thị
} VideoState;

double get_video_clock() {
    double delta = (av_gettime() - frame_timer) / 1000000.0;
    return frame_last_pts + delta;
}
```

### 4. Audio Resampling

```c
// Chuyển đổi audio format
SwrContext *swr_alloc_set_opts(
    NULL,
    AV_CH_LAYOUT_STEREO,         // Output layout
    AV_SAMPLE_FMT_S16,           // Output format
    44100,                       // Output sample rate
    in_channel_layout,           // Input layout
    in_sample_fmt,               // Input format
    in_sample_rate,              // Input sample rate
    0, NULL);

// Convert
swr_convert(swr_ctx, output_buffer, output_size,
            (const uint8_t**)input_buffer, input_size);
```

---

## Data Structures

### Core FFmpeg Structures

| Struct | Mô tả |
|--------|-------|
| `AVFormatContext` | Chứa thông tin về format (streams, metadata, ...) |
| `AVStream` | Thông tin về một stream cụ thể |
| `AVCodecContext` | Thông tin về codec đang sử dụng |
| `AVCodec` | Định nghĩa codec |
| `AVPacket` | Dữ liệu nén (chưa giải mã) |
| `AVFrame` | Dữ liệu đã giải mã (raw) |
| `SwsContext` | Context cho video scaling/color conversion |
| `SwrContext` | Context cho audio resampling |

### Custom Structures

```c
// Video State - struct lớn chứa tất cả thông tin player
typedef struct VideoState {
    AVFormatContext *pFormatCtx;
    int             videoStream, audioStream;
    AVStream        *audio_st;
    AVCodecContext  *audio_ctx;
    AVStream        *video_st;
    AVCodecContext  *video_ctx;

    PacketQueue     videoq;
    PacketQueue     audioq;

    double          audio_clock;
    double          video_clock;
    double          frame_last_pts;
    double          frame_last_delay;
    double          frame_timer;

    int             seek_flags;
    int64_t         seek_pos;

    SDL_Window      *window;
    SDL_Renderer    *renderer;
    SDL_Texture     *texture;
} VideoState;

// Packet Queue
typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;
```

---

## Building & Compilation

Xem [build_guide.md](build_guide.md) để biết chi tiết cách build FFmpeg static libraries và video player.

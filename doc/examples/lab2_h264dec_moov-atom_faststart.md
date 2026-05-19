# H.264 Decoder Architecture

This document describes the H.264/AVC decoder pipeline in FFmpeg's libavcodec.

## Source Files

All H.264 decoder source is in `/app/libavcodec/`:

| File | Purpose |
|------|---------|
| `h264dec.c` | Main decoder, NAL parsing, frame decode entry |
| `h264dec.h` | Core data structures (H264Context, H264SliceContext) |
| `h264_slice.c` | Slice header parsing, macroblock decoding loop |
| `h264_parse.c` | NAL unit extraction |
| `h264_cabac.c` | CABAC entropy decoding |
| `h264_cavlc.c` | CAVLC entropy decoding |
| `h264_pred.c` | Intra prediction |
| `h264_mc_template.c` | Motion compensation |
| `h264_loopfilter.c` | In-loop deblocking filter |
| `h264_refs.c` | Reference frame management |
| `h264_mb.c`, `h264_mb_template.c` | Macroblock-level operations |
| `h264_ps.c` | SPS/PPS parameter set decoding |
| `h264_sei.c` | SEI (Supplemental Enhancement Information) |
| `h264idct.c`, `h264idct_template.c` | Inverse DCT transform |
| `h264_levels.c` | H.264 level constraints |

Platform-specific optimizations:
- `x86/`, `arm/`, `aarch64/`, `mips/`, `ppc/`, `loongarch/`, `riscv/`

---

## Decoder Pipeline

```
                        H.264 DECODER PIPELINE

   INPUT: H.264 Bitstream (Annex B / AVC format)
                │
                ▼
┌───────────────────────────────────────────────────────────────────────────┐
│  1. NAL UNIT PARSING (h264dec.c:decode_nal_units)                         │
│     ┌─────────────────────────────────────────────────────────────────┐   │
│     │  ff_h2645_packet_split()                                        │   │
│     │     │                                                            │   │
│     │     ├─→ Detect AVC vs Annex B format                            │   │
│     │     ├─→ Split by start codes (0x000001) or length fields        │   │
│     │     └─→ Extract NAL units (SPS, PPS, SLICE, SEI, etc.)          │   │
│     └─────────────────────────────────────────────────────────────────┘   │
└───────────────────────────────────────────────────────────────────────────┘
                │
                ▼
┌───────────────────────────────────────────────────────────────────────────┐
│  2. NAL TYPE ROUTING (h264dec.c:590-750)                                  │
│     ┌─────────────────┬─────────────────┬─────────────────┬────────────┐  │
│     │  SPS            │  PPS            │  SLICE          │  SEI       │  │
│     │  (Sequence      │  (Picture       │  (Video Data)   │  (Meta     │  │
│     │   Parameter)    │   Parameter)    │                 │   Info)    │  │
│     │       │         │       │         │       │         │      │     │  │
│     │       ▼         │       ▼         │       ▼         │      ▼     │  │
│     │  Decode SPS     │  Decode PPS     │  ff_h264_      │  Decode    │  │
│     │  → sps_list[]   │  → pps_list[]   │  queue_decode  │  SEI msg   │  │
│     │  (profile,      │  (QP, slice     │  _slice()      │            │  │
│     │   resolution,   │   groups, etc)  │                 │            │  │
│     │   framerate)    │                 │       │         │            │  │
│     └─────────────────┴─────────────────┴───────┼─────────┴────────────┘  │
└─────────────────────────────────────────────────┼─────────────────────────┘
                                                  │
                                                  ▼
┌───────────────────────────────────────────────────────────────────────────┐
│  3. SLICE PROCESSING (h264dec.c:ff_h264_queue_decode_slice)              │
│     ┌─────────────────────────────────────────────────────────────────┐   │
│     │  ┌──────────────────────────────────────────────────────────┐   │   │
│     │  │  ff_h264_execute_decode_slices()                         │   │   │
│     │  │     │                                                     │   │   │
│     │  │     ├─→ h264_slice_header_init()                         │   │   │
│     │  │     │     │ (parse frame num, POC, ref lists, etc.)      │   │   │
│     │  │     │                                                     │   │   │
│     │  │     ├─→ alloc_picture() / find_unused_picture()          │   │   │
│     │  │     │     │ (allocate output frame buffer)               │   │   │
│     │  │     │                                                     │   │   │
│     │  │     └─→ decode_slice() loop per MB row (threaded)        │   │   │
│     │  └──────────────────────────────────────────────────────────┘   │   │
│     └─────────────────────────────────────────────────────────────────┘   │
└───────────────────────────────────────────────────────────────────────────┘
                │
                ▼
┌───────────────────────────────────────────────────────────────────────────┐
│  4. MACROBLOCK DECODING (h264_slice.c)                                    │
│                                                                             │
│     For each Macroblock (16x16):                                           │
│     ┌─────────────────────────────────────────────────────────────────┐   │
│                                                                             │
│     4a. PARSE MB DATA                                                     │
│         ┌───────────────────────┬───────────────────────┐                │
│         │   CABAC               │   CAVLC               │                │
│         │   (h264_cabac.c)      │   (h264_cavlc.c)      │                │
│         │       │               │       │               │                │
│         │       ▼               │       ▼               │                │
│         │   Binary             │   VLC Table           │                │
│         │   arithmetic         │   lookup +            │                │
│         │   decoding           │   coeff parsing       │                │
│         └───────────────────────┴───────────────────────┘                │
│                      │                                                     │
│                      ▼                                                     │
│     4b. RESIDUAL DECODING (h264idct.c / h264idct_template.c)              │
│         ┌───────────────────────────────────────────────────────────┐    │
│         │  ┌─────────────┐   ┌─────────────┐   ┌────────────────┐  │    │
│         │  │  Qp scale   │──→│  iDCT       │──→│  Inverse       │  │    │
│         │  │  + Dequant  │   │  Transform  │   │  Scan          │  │    │
│         │  └─────────────┘   └─────────────┘   └────────────────┘  │    │
│         │        │                                      │           │    │
│         │        └──────────────┬───────────────────────┘           │    │
│         │                       ▼                                    │    │
│         │                Residual Block (DCT coefficients)          │    │
│         └───────────────────────────────────────────────────────────┘    │
│                      │                                                     │
│                      ▼                                                     │
│     4c. PREDICTION (h264pred.c, h264_mc_template.c)                       │
│         ┌───────────────────────┬───────────────────────┐                │
│         │   INTRA               │   INTER               │                │
│         │   ─────               │   ─────               │                │
│         │   I_PCM mode:         │   Motion              │                │
│         │   Raw pixel copy      │   Compensation        │                │
│         │                       │   (MC)                │                │
│         │   Intra pred modes:   │       │               │                │
│         │   • DC (flat)         │       ▼               │                │
│         │   • H/V/Diagonal      │   Reference           │                │
│         │   • Plane             │   frame lookup        │                │
│         │                       │       │               │                │
│         │   For each 4x4/8x8:   │       ▼               │                │
│         │   • Use neighboring   │   Sub-pixel           │                │
│         │     pixels as ref     │   interpolation       │                │
│         │                       │   (1/4 pixel)         │                │
│         └───────────────────────┴───────────────────────┘                │
│                      │                                                     │
│                      ▼                                                     │
│     4d. RECONSTRUCTION                                                   │
│         ┌───────────────────────────────────────────────────────────┐    │
│         │                                                           │    │
│         │   Residual ────────────────────▶ Reconstructed Block      │    │
│         │      +                                                  │    │
│         │   Predicted ──────────────────────────────────────────   │    │
│         │                                                           │    │
│         │   Clip to valid range [0, 255] for 8-bit                  │    │
│         │                                                           │    │
│         └───────────────────────────────────────────────────────────┘    │
│                                                                             │
└───────────────────────────────────────────────────────────────────────────┘
                │
                ▼
┌───────────────────────────────────────────────────────────────────────────┐
│  5. DEBLOCKING FILTER (h264_loopfilter.c)                                 │
│                                                                             │
│     ┌─────────────────────────────────────────────────────────────────┐   │
│     │  ff_h264_filter_mb() / ff_h264_filter_mb_fast()                │   │
│     │     │                                                            │   │
│     │     ├─→ Analyze block edges (boundary strength)                 │   │
│     │     │                                                            │   │
│     │     ├─→ Decide filter strength (strong/weak based on QP, MV)    │   │
│     │     │                                                            │   │
│     │     ├─→ Apply filter:                                            │   │
│     │     │   • Modify edge pixels                                     │   │
│     │     │   • Reduce blocking artifacts                              │   │
│     │     │   • Preserve edge sharpness where needed                  │   │
│     │     │                                                            │   │
│     │     └─→ Process: Luma → Chroma (separate filters)               │   │
│     └─────────────────────────────────────────────────────────────────┘   │
└───────────────────────────────────────────────────────────────────────────┘
                │
                ▼
┌───────────────────────────────────────────────────────────────────────────┐
│  6. PICTURE OUTPUT                                                        │
│     ┌─────────────────────────────────────────────────────────────────┐   │
│     │  ff_h264_field_end()                                            │   │
│     │     │                                                            │   │
│     │     ├─→ Field combining (if interlaced)                         │   │
│     │     │                                                            │   │
│     │     ├─→ DPB Management (Decoded Picture Buffer)                 │   │
│     │     │     │                                                      │   │
│     │     │     ├─→ Output reordering (for B-frames/POC sorting)     │   │
│     │     │     ├─→ Reference list update (MMCO operations)          │   │
│     │     │     └─→ Mark frames as short/long term reference          │   │
│     │     │                                                            │   │
│     │     ├─→ ff_h264_execute_ref_pic_marking()                       │   │
│     │     │                                                            │   │
│     │     └─→ finalize_frame() → Output decoded picture               │   │
│     └─────────────────────────────────────────────────────────────────┘   │
└───────────────────────────────────────────────────────────────────────────┘
                │
                ▼
   OUTPUT: Decoded YUV Frame (AVFrame)
```

---

## Key Data Structures

### H264Context (Main Decoder State)

```c
typedef struct H264Context {
    AVCodecContext *avctx;
    H264Picture DPB[H264_MAX_PICTURE_COUNT];  // Decoded Picture Buffer
    H264SliceContext *slice_ctx;               // Per-thread slice context
    H264ParamSets ps;                          // SPS/PPS parameter sets
    H264POCContext poc;                        // Picture Order Count tracking
    int width, height;                         // Frame dimensions
    int nal_ref_idc, nal_unit_type;           // Current NAL info
    int picture_idr;                           // Is current picture IDR?
} H264Context;
```

### H264SliceContext (Per-Slice State)

```c
typedef struct H264SliceContext {
    GetBitContext gb;                          // Bitstream reader position
    CABACContext cabac;                        // CABAC decoder state
    int mb_x, mb_y;                           // Current macroblock position
    int mb_xy;                                // Flattened MB index
    H264Ref ref_list[2][48];                  // Reference frame lists (L0/L1)
    uint8_t non_zero_count_cache[15 * 8];    // Coded block pattern cache
    int16_t mv_cache[2][5 * 8][2];           // Motion vector cache
    int8_t ref_cache[2][5 * 8];              // Reference index cache
    int qscale;                               // Quantization parameter
    int slice_type;                           // I/P/B/SI/SP
} H264SliceContext;
```

### H264Picture (Decoded Frame)

```c
typedef struct H264Picture {
    AVFrame *f;                               // Output frame buffer
    int poc;                                  // Picture Order Count
    int frame_num;                            // Frame number
    int reference;                            // Reference status (short/long term)
    int long_ref;                             // 1=long term, 0=short term reference
    int mb_width, mb_height;                  // Macroblock dimensions
    int8_t *qscale_table;                     // Per-MB QP values
    uint32_t *mb_type;                        // Per-MB types
} H264Picture;
```

---

## Decoding Modes

| Mode | File | Description |
|------|------|-------------|
| **CABAC** | `h264_cabac.c` | Context-based Adaptive Binary Arithmetic Coding (default, better compression) |
| **CAVLC** | `h264_cavlc.c` | Context-based Adaptive Variable Length Coding (simpler, baseline profile) |

---

## Slice Types

| Type | Prediction | Notes |
|------|------------|-------|
| **I** | Intra only | No motion compensation |
| **P** | Inter (L0) | Forward prediction only |
| **B** | Inter (L0/L1) | Bidirectional prediction |
| **SI** | Intra/Switch | Error resilience |
| **SP** | Inter/Switch | Switching pictures |

---

## NAL Unit Types

| Type | Name | Description |
|------|------|-------------|
| 1 | Non-IDR slice | Regular video slice |
| 2 | DP A slice | Data partitioned A |
| 3 | DP B slice | Data partitioned B |
| 4 | DP C slice | Data partitioned C |
| 5 | IDR slice | Instantaneous Decoding Refresh (keyframe) |
| 6 | SEI | Supplemental Enhancement Information |
| 7 | SPS | Sequence Parameter Set |
| 8 | PPS | Picture Parameter Set |
| 9 | Access Unit Delimiter | Marks access unit boundary |
| 10 | End of Sequence | Marks video sequence end |
| 11 | End of Stream | Marks bitstream end |

---

## Decode Flow Summary

```
h264_decode_frame()           // Main entry point (h264dec.c)
    │
    ├─→ ff_h2645_packet_split()        // Split NAL units
    │
    ├─→ decode_nal_units()             // Process each NAL
    │       │
    │       ├─→ SPS → ff_h264_decode_seq_parameter_set()
    │       ├─→ PPS → ff_h264_decode_picture_parameter_set()
    │       ├─→ SEI → ff_h264_sei_decode()
    │       └─→ SLICE → ff_h264_queue_decode_slice()
    │               │
    │               └─→ ff_h264_execute_decode_slices()
    │                       │
    │                       ├─→ h264_slice_header_init()
    │                       ├─→ alloc_picture()
    │                       └─→ decode_slice()  // Per-MB loop
    │                               │
    │                               ├─→ CABAC/CAVLC decode
    │                               ├─→ Residual iDCT
    │                               ├─→ Intra/Inter prediction
    │                               ├─→ Reconstruction (residual + pred)
    │                               └─→ Deblocking filter
    │
    ├─→ ff_h264_field_end()            // End of field/frame
    │       │
    │       ├─→ DPB management (reordering, MMCO)
    │       └─→ finalize_frame()
    │
    └─→ Output AVFrame
```

---

## Container Timing & Audio/Video Sync

The H264 decoder itself does **not** handle timing or audio/video synchronization. That is handled at the **container/demuxer level** in libavformat.

### MP4/MOV Container Structure

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          MP4/MOV File Structure                         │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────┐   ┌─────────┐   ┌─────────┐   ┌─────────┐
│  ftyp   │   │  moov   │   │  mdat   │   │  mfra   │
│ (file   │──▶│ (movie) │   │ (media  │   │ (movie  │
│  type)  │   │   box)  │   │  data)  │   │ fragment│
└─────────┘   └────┬────┘   └─────────┘   │  random │
                   │                      └─────────┘
                   ▼
            ┌────────────────┐
            │  moov.trak     │  ← One per stream (video, audio, subtitle)
            │  (track box)   │
            └────────┬───────┘
                     │
        ┌────────────┼────────────┐
        ▼            ▼            ▼
┌──────────────┐ ┌──────────┐ ┌──────────┐
│   mvhd       │ │   tkhd   │ │   udta   │
│ (movie header)│ │(track hdr)│ │ (user   │
│              │ │          │ │  data)  │
└──────────────┘ └──────────┘ └──────────┘
        │            │
        ▼            ▼
┌──────────────┐ ┌──────────────────────────────────────────────┐
│  timescale   │ │                mdia                           │
│  duration    │ │  ┌────────────────────────────────────────┐  │
│  (mvhd)      │ │  │  mdhd: media header (timescale)        │  │
└──────────────┘ │  │  hdlr: handler (video/audio)           │  │
                 │  └────────────────────────────────────────┘  │
                 │  ┌────────────────────────────────────────┐  │
                 │  │  minf: media information                │  │
                 │  │  ┌──────────────────────────────────┐  │  │
                 │  │  │  stbl: sample table               │  │  │
                 │  │  │  ┌────┐ ┌────┐ ┌────┐ ┌────┐    │  │  │
                 │  │  │  │stts│ │stss│ │stsc│ │stco│    │  │  │
                 │  │  │  │  │  │  │  │  │  │  │  │    │  │  │
                 │  │  │  │  │  │  │  │  │  │  │  │    │  │  │
                 │  │  │  │  ▼  │  ▼  │  ▼  │  ▼  │    │  │  │
                 │  │  │  │ DTS │Sync │Chunk│Offset│    │  │  │
                 │  │  │  │table│table│table│table │    │  │  │
                 │  │  │  └────┘ └────┘ └────┘ └────┘    │  │  │
                 │  │  │  ┌────┐ ┌────┐ ┌────┐           │  │  │
                 │  │  │  │ctts│ │cslg│ │sgpd│           │  │  │
                 │  │  │  │  │  │  │  │  │  │            │  │  │
                 │  │  │  │  ▼  │  ▼  │  ▼  │            │  │  │
                 │  │  │  │PTS │Cts  │Sync │            │  │  │
                 │  │  │  │off │info │group│            │  │  │
                 │  │  │  └────┘ └────┘ └────┘           │  │  │
                 │  │  └──────────────────────────────────┘  │  │
                 │  └────────────────────────────────────────┘  │
                 │  ┌────────────────────────────────────────┐  │
                 │  │  meta: metadata (optional)              │  │
                 │  └────────────────────────────────────────┘  │
                 └──────────────────────────────────────────────┘
```

### Timing Atoms in moov Box

| Atom | Name | Purpose |
|------|------|---------|
| `mvhd` | movie header | Overall timescale, duration, creation time |
| `tkhd` | track header | Track ID, duration, volume (audio), dimensions |
| `mdhd` | media header | Media timescale, language |
| `stts` | time-to-sample | Maps samples → decode time (DTS) |
| `ctts` | composition time-to-sample | Maps samples → presentation time (PTS) |
| `stss` | sync sample | Marks keyframes (I-frames) |
| `stsc` | sample-to-chunk | Maps samples → file chunks |
| `stco` | chunk offset | Byte offset of chunks in file |
| `cslg` | composition to decode timeline | B-frame reordering offset |
| `elst` | edit list | Maps timeline (includes start delay, trim) |
| `sgpd` | sample group description | Groups samples (e.g., SAP types) |
| `sbgp` | sample-to-group | Sample → group mapping |

### Key Files for Container Timing

| File | Purpose |
|------|---------|
| `/app/libavformat/mov.c` | MP4/MOV demuxer - parses moov atoms |
| `/app/libavformat/utils.c` | General demuxer utilities, timestamp conversion |
| `/app/fftools/ffplay.c` | Player with AV sync logic |
| `/app/fftools/ffmpeg.c` | Transcoder with PTS/DTS handling |

### Edit List (elst) - Timeline Mapping

The edit list handles audio/video synchronization when:
1. Video starts later than audio (intro/silence)
2. Video needs to be trimmed
3. There's a delay between media decode and presentation

```c
// mov.c:mov_read_elst() - Parse edit list atom
typedef struct MOVElst {
    int64_t duration;  // Edit duration in movie timescale
    int64_t time;      // Media time (-1 = empty edit/pause)
    float rate;        // Playback rate (usually 1.0)
} MOVElst;
```

### DTS vs PTS - Decode vs Presentation Time

```
┌─────────────────────────────────────────────────────────────────────────┐
│              DTS/PTS Timeline with B-frame Reordering                  │
└─────────────────────────────────────────────────────────────────────────┘

   Decode Order (bitstream):     P3    B0    B1    P6    B4    B5
                                  │      │     │     │     │     │
                                  ▼      ▼     ▼     ▼     ▼     ▼
   DTS (Decode Timestamp):       3      0     1     6     4     5
                                  
   Reorder Buffer:              [P3]   [B0] [B1,P3] [B1] [P6] [B4] [B5]
                                  │     /│    /│     │     │     │
                                  ▼    / ▼   / ▼     ▼     ▼     ▼
   PTS (Presentation Time):     P3    B0   B1   P6    B4    B5
                                  
   Display Order:               P3    B0    B1    P6    B4    B5
```

### Composition Time Offset (ctts)

```c
// ctts_data maps each sample to its composition time offset
// PTS = DTS + ctts_offset

// Example (mov.c:mov_read_ctts):
typedef struct MOVCtts {
    int count;     // Number of consecutive samples
    int offset;    // Composition time offset (signed)
} MOVCtts;
```

### Audio/Video Sync in FFplay

FFplay synchronizes using one of three methods (ffplay.c):

```c
// Sync modes (ffplay.c:1431)
typedef enum {
    AV_SYNC_AUDIO_MASTER,      // Sync to audio clock (default)
    AV_SYNC_VIDEO_MASTER,      // Sync to video clock
    AV_SYNC_EXTERNAL_CLOCK,    // Sync to external clock (network)
} AVSyncType;
```

**Sync Algorithm** (ffplay.c:compute_target_delay):

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    Video Frame Timing Adjustment                        │
└─────────────────────────────────────────────────────────────────────────┘

   master_clock (audio) ──────▶ ┌──────────┐
                                │  diff    │ = vid_clock - master_clock
   vid_clock (video)  ─────────▶ │(compare) │
                                └─────┬────┘
                                      │
                                      ▼
                              ┌───────────────┐
                              │ |diff| < threshold ?
                              └───────┬───────┘
                                    │
                    ┌───────────────┼───────────────┐
                    │YES                           │NO
                    ▼                               ▼
        ┌───────────────────┐           ┌───────────────────┐
        │  Frame on time    │           │  Adjust timing    │
        │  (no correction)  │           │  (skip/dup frame) │
        └───────────────────┘           └───────────────────┘
                                                 │
                            ┌────────────────────┼────────────────────┐
                            │                    │                    │
                            ▼                    ▼                    ▼
                   diff < -threshold      diff > +threshold      diff > framedup
                            │                    │                    │
                            ▼                    ▼                    ▼
                   delay + diff          delay + 2*diff        delay + diff
```

### Timestamp Rescaling Pipeline

```
Packet Input (demuxed)
        │
        ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  1. Read raw timestamp from container (stts/ctts/elst)                 │
│     mov.c:mov_read_stts() → sample.cts / sample.dts                     │
│                                                                         
│  2. Apply edit list offset (elst)                                      │
│     mov.c:get_edit_list_entry() → adjusted_dts                         │
│                                                                         
│  3. Apply ctts (composition time offset)                               │
│     pts = dts + ctts_offset                                            │
│                                                                         
│  4. Rescale to stream timebase                                         │
│     av_rescale_q(pts, {num, den}, stream->time_base)                   │
│                                                                         
│  5. Rescale to output timebase (if different)                          │
│     av_rescale_q(pts, stream->time_base, out_time_base)                │
└─────────────────────────────────────────────────────────────────────────┘
        │
        ▼
   Frame pts (in AVFrame)
        │
        ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  6. FFplay sync (ffplay.c:video_refresh)                               │
│     target_delay = compute_target_delay(                               │
│         frame_duration,                                                 │
│         get_clock(&vidclk) - get_master_clock()                        │
│     )                                                                   │
│                                                                         
│  7. Schedule frame display at correct wall-clock time                  │
│     frame_timer += target_delay                                        │
└─────────────────────────────────────────────────────────────────────────┘
```

### Sync-Related Code Locations

| Component | File | Key Function | Purpose |
|-----------|------|--------------|---------|
| **Edit List** | `mov.c` | `mov_read_elst()` | Parse elst atom |
| **Edit List Apply** | `mov.c` | `get_edit_list_entry()` | Get media time/duration |
| **DTS Table** | `mov.c` | `mov_read_stts()` | Parse stts atom (DTS) |
| **PTS Offset** | `mov.c` | `mov_read_ctts()` | Parse ctts atom (PTS offset) |
| **Keyframe Map** | `mov.c` | `mov_read_stss()` | Parse sync sample table |
| **Frame Search** | `mov.c` | `find_previous_skip_point()` | Find nearest keyframe |
| **AV Sync** | `ffplay.c` | `compute_target_delay()` | Calculate frame timing |
| **Clock Sync** | `ffplay.c` | `get_master_sync_type()` | Get sync mode |
| **Frame Display** | `ffplay.c` | `video_refresh()` | Display loop with sync |

### Example: iTunes M4V with Delay

When iTunes encodes video, often there's an edit list like:

```
Edit List Entry 0:
  duration = 0          (pause/empty)
  media_time = -1       (no media)
  rate = 1.0

Edit List Entry 1:
  duration = <video_duration>  (play video)
  media_time = 0               (from sample 0)
  rate = 1.0
```

This handles the "gap" before video starts (during intro).

---

## References

- ITU-T H.264 Specification: https://www.itu.int/rec/T-REC-H.264
- FFmpeg libavcodec/h264: `/app/libavcodec/h264*`
- FFmpeg libavformat/mov: `/app/libavformat/mov.c`
- FFmpeg ffplay sync: `/app/fftools/ffplay.c`

---

## Streaming / Incomplete Files: Moov at End

When the moov atom is at the **end** of the file (typical MP4 without faststart) and the file is still being written (streaming, live recording), FFmpeg faces a critical challenge.

### The Problem

```
┌─────────────────────────────────────────────────────────────────────────┐
│              Normal MP4 (moov before mdat)                              │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐                            │
│  │   ftyp   │──▶│   moov   │──▶│   mdat   │                            │
│  └──────────┘   └──────────┘   └──────────┘                            │
│                    │                                                      │
│                    └──▶ Can read timing info BEFORE decoding data       │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│              Streaming MP4 (moov at end)                                │
│  ┌──────────┐   ┌──────────┐                            ┌──────────┐   │
│  │   ftyp   │──▶│   mdat   │ ─ ─ ─ writing...           │   moov   │   │
│  └──────────┘   └──────────┘                            └──────────┘   │
│                    │                                         │          │
│                    │                                         │          │
│                    └──▶ moov NOT available yet!              │          │
│                              No timing info, stts, ctts, etc │          │
└─────────────────────────────────────────────────────────────────────────┘
```

### What Happens Without moov

| Atom Missing | Impact |
|--------------|--------|
| `stts` | No sample durations, no DTS timing |
| `ctts` | No PTS offsets (B-frame reordering broken) |
| `stss` | No keyframe list (can't seek properly) |
| `stco` | No chunk offsets in file |
| `elst` | No timeline/edit information |

### FFmpeg's Behavior Without moov

When moov isn't available during streaming, FFmpeg falls back to:

```c
// demux.c - AVFMT_FLAG_GENPTS generates timestamps when missing
if (s->flags & AVFMT_FLAG_GENPTS) {
    // FFmpeg infers timestamps from frame rate and sample count
    // pkt->pts = inferred_value
    // pkt->dts = inferred_value
}

// mov.c:10790-10800 - When ctts is missing:
if (sc->ctts_count && sc->tts_index < sc->tts_count) {
    pkt->pts = av_sat_add64(pkt->dts, sc->dts_shift + offset);
} else {
    // Fallback: PTS = DTS (no B-frame reordering!)
    pkt->pts = pkt->dts;
}
```

### The Sync Problem

```
┌─────────────────────────────────────────────────────────────────────────┐
│         Without proper moov, AV sync is NOT guaranteed                 │
└─────────────────────────────────────────────────────────────────────────┘

SCENARIO: Live stream with moov at end

Video Track:
  - PTS/DTS inferred from frame rate (e.g., 30fps)
  - Frame 0: pts=0, Frame 1: pts=33333, Frame 2: pts=66666, ...
  - Assumes constant frame rate - breaks if VBR or variable GOP

Audio Track:
  - Usually AAC with explicit timing in ADTS header
  - More reliable timing from codec

Problem:
  ┌────────────────────────────────────────────────────────────┐
  │ Video: Inferred pts based on frame# × (1/30s)             │
  │ Audio: Real timestamps from AAC samples                   │
  │                                                             │
  │ These don't necessarily match!                            │
  │                                                             │
  │ Video could be ahead or behind audio due to:              │
  │ - Variable frame rate encoding                            │
  │ - Scene changes affecting frame timing                    │
  │ - B-frame count differences                               │
  │ - Keyframe spacing (not all frames are keyframes)         │
  └────────────────────────────────────────────────────────────┘
```

### Solutions

**1. Use Annex B HLS/MPEG-TS (Recommended for Streaming)**

MPEG-TS doesn't have moov - it has PCR (Program Clock Reference) for sync:
```
┌─────────────────────────────────────────────────────────────────────────┐
│                      MPEG-TS Streaming                                  │
└─────────────────────────────────────────────────────────────────────────┘

┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐
│  PAT   │ │  PMT   │ │  PES   │ │  PES   │
│(Prog   │ │(Video) │ │(Video) │ │(Audio) │
│ List)  │ │        │ │  PTS   │ │  PTS   │
└────────┘ └────────┘ └────────┘ └────────┘
             │
             ├──▶ PTS/DTS embedded in each PES packet
             ├──▶ PCR (Program Clock Reference) for sync
             └──▶ No moov needed - self-describing
```

**2. Use faststart for MP4**

When creating the file, use `-movflags +faststart` to put moov before mdat:
```bash
ffmpeg -i input.mp4 -movflags +faststart output.mp4
# moov is moved to beginning, readable before mdat
```

**3. Use fragmented MP4 (fMP4)**

Fragmented MP4 has timing in each fragment:
```bash
ffmpeg -i input -movflags +frag_keyframe+default_base_moof output.mp4
# Each fragment has its own moof with timing
```

**4. Wait for moov (Progressive Download)**

FFmpeg will attempt to seek back for moov when seekable:
```c
// mov.c:10456-10463
if (mov->moov_retry)
    avio_seek(pb, 0, SEEK_SET);  // Go back to find moov
// Continues until moov is found (if file is complete)
```

### FFmpeg Options for Streaming

| Option | Purpose |
|--------|---------|
| `-fflags +genpts` | Generate pts when missing (default for non-seekable) |
| `-fflags +ignidx` | Ignore index, rely on genpts |
| `-ignore_editlist 1` | Ignore edit list atom |
| `-use_wallclock_as_timestamps 1` | Use wallclock time for PTS |
| `-avioflags direct` | Disable buffering for low latency |

### Code Paths for Incomplete Files

| Scenario | Code Path | Result |
|----------|-----------|--------|
| **moov at end, seekable** | `mov.c:10462` retries seeking | Waits for complete file |
| **moov at end, NOT seekable** | `mov.c:10789-10800` | PTS = DTS (no B-reorder) |
| **Live stream (no moov)** | `demux.c:1550` genpts mode | Inferred timestamps |
| **fMP4 fragments** | `mov.c:mov_read_moof` | Each fragment has timing |
| **MPEG-TS** | `mpegts.c` | PCR-based sync |

### Summary

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Can A/V go out of sync when moov is at the end?                        │
│                                                                          
│  ANSWER: YES, HIGH RISK                                                   │
│                                                                          
│  Without moov:                                                            │
│  ├── Video: Timestamps inferred from frame# × fps (unreliable)          │
│  ├── Audio: Real timestamps from AAC codec (reliable)                   │
│  └── Result: Desync possible due to:                                     │
│              - Variable frame rate                                       │
│              - Missing B-frame reordering info                           │
│              - No keyframe map for proper seeking                        │
│                                                                          
│  Best solutions:                                                          │
│  1. Use MPEG-TS for streaming (built-in sync)                           │
│  2. Use fragmented MP4 (fMP4)                                            │
│  3. Wait for moov (progressive download)                                 │
│  4. Use faststart when creating MP4                                      │
└─────────────────────────────────────────────────────────────────────────┘
```
# FFmpeg Video Cutting Without Reencoding (`-c copy`)

## Command

```bash
ffmpeg -y -ss 0 -i input.mp4 -t 7 -map 0 -c copy output_0-10.mp4
```

This command cuts 7 seconds from `input.mp4` using direct stream copy (no re-encoding).

---

## Complete Flow Chart

```
main() [ffmpeg.c:946]
    └── ffmpeg_parse_options() [ffmpeg_opt.c:1376]
            ├── open_files("input") → ifile_open() [ffmpeg_demux.c:1588]
            │       ├── avformat_open_input()
            │       ├── avformat_find_stream_info()
            │       └── avformat_seek_file() → -ss 0
            │
            └── open_files("output") → of_open() [ffmpeg_mux_init.c:3262]
                    ├── avformat_alloc_output_context2()
                    ├── create_streams() → -map 0
                    │       └── ost_add() [stream copy: no encoder]
                    └── avio_open2()
                            │
                            ▼
                    transcode() [ffmpeg.c:855]
                            │
                            ▼
                    ┌──────────────────────────────────────┐
                    │     DEMUXER THREAD (ffmpeg_demux.c)   │
                    │  av_read_frame() → sch_demux_send()   │
                    └──────────────────────────────────────┘
                            │
                            ▼
                    ┌──────────────────────────────────────┐
                    │       MUXER THREAD (ffmpeg_mux.c)     │
                    │  sch_mux_receive() → of_streamcopy()  │
                    │       ├── -t 7: check duration        │
                    │       ├── -ss 0: apply ts offset      │
                    │       └── av_interleaved_write_frame()│
                    └──────────────────────────────────────┘
                            │
                            ▼
                    of_write_trailer() [ffmpeg_mux.c:757]
```

---

## Key Functions by Stage

| Stage | File | Function | Line |
|-------|------|----------|------|
| Option Parsing | ffmpeg_opt.c | `ffmpeg_parse_options()` | 1376 |
| Input Opening | ffmpeg_demux.c | `ifile_open()` | 1588 |
| Output Creation | ffmpeg_mux_init.c | `of_open()` | 3262 |
| Demuxer Loop | ffmpeg_demux.c | `input_thread()` | 682 |
| Timestamp Fixup | ffmpeg_demux.c | `input_packet_process()` | 447 |
| Muxer Loop | ffmpeg_mux.c | `muxer_thread()` | 407 |
| Stream Copy | ffmpeg_mux.c | `of_streamcopy()` | 460 |
| Packet Write | ffmpeg_mux.c | `write_packet()` | 209 |

---

## 1. Demuxer Thread: `input_thread()`

**Location:** `fftools/ffmpeg_demux.c:682`

```c
static int input_thread(void *arg)
{
    Demuxer *d = arg;
    InputFile *f = &d->f;

    demux_thread_init(&dt);
    thread_set_name(f);
    discard_unused_programs(f);

    d->read_started    = 1;
    d->wallclock_start = av_gettime_relative();

    while (1) {
        DemuxStream *ds;
        unsigned send_flags = 0;

        // 1. READ PACKET from input file
        ret = av_read_frame(f->ctx, dt.pkt_demux);

        if (ret == AVERROR(EAGAIN)) {
            av_usleep(10000);  // Retry if no packet ready
            continue;
        }
        if (ret < 0) {
            // EOF or error - flush BSF, handle looping
            if (d->loop) {
                seek_to_start(d, ...);
                continue;
            }
            break;  // Exit loop
        }

        // 2. CHECK STREAM VALIDITY
        ds = ds_from_ist(f->streams[pkt->stream_index]);
        if (!ds || ds->discard || ds->finished) {
            av_packet_unref(pkt);
            continue;  // Skip unwanted streams
        }

        // 3. PROCESS PACKET (timestamp fixup)
        input_packet_process(d, pkt, &send_flags);

        // 4. READ RATE THROTTLING
        if (d->readrate)
            readrate_sleep(d);

        // 5. SEND TO SCHEDULER
        demux_send(d, &dt, ds, pkt, send_flags);
    }

    demux_thread_uninit(&dt);
    return ret;
}
```

### Loop Termination Conditions

| Condition | Action | Reason |
|-----------|--------|--------|
| `av_read_frame()` returns `AVERROR_EOF` | Break | End of file reached |
| `av_read_frame()` returns error | Break | Read error |
| `input_packet_process()` returns < 0 | Break | Processing error |
| `demux_send()` returns < 0 | Break | Send failed |
| `d->loop` set | `seek_to_start()` + continue | Looping enabled |

---

## 2. Timestamp Processing: `input_packet_process()`

**Location:** `fftools/ffmpeg_demux.c:447`

```
av_read_frame() reads raw packet from container
        │
        ▼
input_packet_process()
        │
        ├── ts_fixup()          ← Main timestamp adjustment
        │       │
        │       ├── 1. PTS wrap correction (if pts_wrap_bits < 64)
        │       ├── 2. Apply ts_offset (from -itsoffset or -ss)
        │       ├── 3. Apply ts_scale (from -setpts/-r)
        │       ├── 4. Add duration offset (for seek position)
        │       ├── 5. ts_discontinuity_detect() → adjust if gap detected
        │       └── 6. ist_dts_update() → update stream's DTS tracker
        │
        └── Check recording_time (-t) → set DEMUX_SEND_STREAMCOPY_EOF if past duration
                │
                ▼
        demux_send() → sch_demux_send() → Muxer receives packet
```

### Step-by-Step PTS/DTS Handling

#### 1. PTS Wrap Correction (lines 380-396)
Handles timestamp overflow when `pts_wrap_bits < 64`:

```c
// If timestamps exceed the wrap threshold, subtract 2^wrap_bits
if (pkt->dts > stime + (1LL << (pts_wrap_bits-1))) {
    pkt->dts -= 1ULL << pts_wrap_bits;
}
if (pkt->pts > stime + (1LL << (pts_wrap_bits-1))) {
    pkt->pts -= 1ULL << pts_wrap_bits;
}
```

#### 2. Apply ts_offset (lines 398-401)
Offsets timestamps by the file's start time offset (from `-ss` or `-itsoffset`):

```c
// Converts ts_offset from AV_TIME_BASE to stream's time_base
pkt->dts += av_rescale_q(ifile->ts_offset, AV_TIME_BASE_Q, pkt->time_base);
pkt->pts += av_rescale_q(ifile->ts_offset, AV_TIME_BASE_Q, pkt->time_base);
```

#### 3. Apply ts_scale (lines 403-406)
Scales timestamps for `-setpts` filter or `-r` rate change:

```c
pkt->pts *= ds->ts_scale;
pkt->dts *= ds->ts_scale;
```

#### 4. Add Duration Offset (lines 408-432)
Adjusts for the seek position so timestamps start from 0 (or seek point):

```c
duration = av_rescale_q(d->duration.ts, d->duration.tb, pkt->time_base);
pkt->pts += duration;  // Shift PTS by duration
pkt->dts += duration;  // Shift DTS by duration
```

#### 5. Discontinuity Detection (lines 437, 276-294)
Detects gaps in timestamps and corrects them:

```c
// Apply any previous discontinuity offset
pkt->dts += offset;
pkt->pts += offset;

// Detect new discontinuities
ts_discontinuity_detect(d, ist, pkt);
    └── If delta > dts_delta_threshold:
        d->ts_offset_discont -= delta;
        pkt->dts -= delta;
        pkt->pts -= delta;
```

#### 6. Update Stream DTS Tracker (lines 440, 296-354)
Maintains expected next DTS for the stream:

```c
ist_dts_update(ds, pkt, fd) {
    if (!ds->saw_first_ts) {
        // Initialize from first packet
        ds->dts = pkt->pts (scaled to AV_TIME_BASE);
    }

    // Update current DTS
    ds->dts = av_rescale_q(pkt->dts, pkt->time_base, AV_TIME_BASE_Q);

    // Calculate next expected DTS based on codec type
    switch (codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            ds->next_dts += AV_TIME_BASE * frame_size / sample_rate;
        case AVMEDIA_TYPE_VIDEO:
            ds->next_dts += av_rescale_q(pkt->duration, ...);
    }
}
```

### For `-ss 0 -t 7` Command

```
av_read_frame() → packet from file
        │
        ▼
ts_fixup()
        │
        ├── ts_offset = 0 (start_time_effective = 0)
        │   → No adjustment needed
        │
        ├── ts_scale = 1.0 (default)
        │   → No scaling
        │
        ├── duration = 0 (seek position is 0)
        │   → No offset added
        │
        ├── No discontinuities detected
        │   → No correction needed
        │
        └── ist_dts_update()
                ds->dts = packet DTS (unchanged)
                ds->next_dts = dts + frame_duration
        │
        ▼
Check -t 7:
    if (ds->dts >= recording_time + start_time)
        set DEMUX_SEND_STREAMCOPY_EOF flag
        (muxer will stop writing packets)
```

---

## 3. Scheduler: Packet Routing

### Packet Flow: Demuxer → Scheduler → Muxer

```
┌─────────────────────────────────────────────────────────────────────┐
│                        DEMUXER THREAD                               │
│  input_thread()                                                      │
│      │                                                               │
│      ├── av_read_frame() → pkt                                       │
│      │                                                               │
│      ├── input_packet_process() → process timestamps                 │
│      │                                                               │
│      └── demux_send()                                                │
│              │                                                       │
│              ▼                                                       │
│          sch_demux_send()  [ffmpeg_sched.c:2029]                     │
│                  │                                                   │
│                  ▼                                                   │
│          demux_send_for_stream()  [ffmpeg_sched.c:1953]              │
│                  │                                                   │
│                  ▼                                                   │
│          demux_stream_send_to_dst()  [ffmpeg_sched.c:1918]           │
│                  │                                                   │
│                  ├── For MUX destination: send_to_mux()              │
│                  │         │                                         │
│                  │         ▼                                         │
│                  │     tq_send(mux->queue, stream_idx, pkt)          │
│                  │         [thread_queue.c]                          │
│                  │         │                                         │
│                  │         ▼                                         │
│                  │     av_fifo_writer_write() → push to FIFO         │
│                  │                                                   │
│                  └── For DEC destination: tq_send(dec->queue, ...)   │
└─────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ ThreadQueue (FIFO)
                                    │ Contains: pkt + stream_idx
                                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         MUXER THREAD                                │
│  muxer_thread()  [ffmpeg_mux.c:407]                                  │
│      │                                                               │
│      ├── sch_mux_receive()  [ffmpeg_sched.c:2073]                    │
│      │         │                                                       │
│      │         ▼                                                       │
│      │     tq_receive(mux->queue, &stream_idx, pkt)                  │
│      │         [thread_queue.c:193]                                  │
│      │         │                                                     │
│      │         ├── pthread_mutex_lock()                              │
│      │         ├── check if data available in FIFO                   │
│      │         ├── av_container_fifo_read() → pop from FIFO          │
│      │         ├── pthread_mutex_unlock()                            │
│      │         └── return stream_idx + pkt                           │
│      │                                                               │
│      ├── ost = of->streams[stream_idx]                               │
│      │                                                               │
│      └── mux_packet_filter() → write_packet() → output file          │
└─────────────────────────────────────────────────────────────────────┘
```

### Key Functions

#### 1. Demuxer sends packet (`sch_demux_send`)
```c
// ffmpeg_sched.c:2029
int sch_demux_send(Scheduler *sch, unsigned demux_idx, AVPacket *pkt, unsigned flags)
{
    d = &sch->demux[demux_idx];
    waiter_wait(sch, &d->waiter);  // Wait for slot
    return demux_send_for_stream(sch, d, &d->streams[pkt->stream_index], pkt, flags);
}
```

#### 2. Route to muxer (`demux_stream_send_to_dst`)
```c
// ffmpeg_sched.c:1918
ret = demux_stream_send_to_dst(sch, ds->dst[i], finished, to_send, flags);
// If destination is MUX:
send_to_mux(sch, &sch->mux[dst.idx], dst.idx_stream, pkt);
```

#### 3. Send to queue (`send_to_mux` → `tq_send`)
```c
// ffmpeg_sched.c:1880-1896
ret = tq_send(mux->queue, stream_idx, pkt);
// Pushes packet + stream_idx to FIFO buffer
```

#### 4. Muxer receives from queue (`sch_mux_receive` → `tq_receive`)
```c
// ffmpeg_sched.c:2073
int sch_mux_receive(Scheduler *sch, unsigned mux_idx, AVPacket *pkt)
{
    mux = &sch->mux[mux_idx];
    ret = tq_receive(mux->queue, &stream_idx, pkt);  // BLOCKING CALL
    pkt->stream_index = stream_idx;
    return ret;
}

// ffmpeg_mux.c:426
ret = sch_mux_receive(mux->sch, of->index, mt.pkt);
stream_idx = mt.pkt->stream_index;
if (stream_idx < 0) {
    // EOF - all streams finished
    break;
}
```

---

## 4. Muxer Thread: `muxer_thread()`

**Location:** `fftools/ffmpeg_mux.c:407`

```c
int muxer_thread(void *arg)
{
    Muxer *mux = arg;
    OutputFile *of = &mux->of;

    mux_thread_init(&mt);
    thread_set_name(mux);

    while (1) {
        OutputStream *ost;
        int stream_idx, stream_eof = 0;

        // BLOCKING: Wait for packet from demuxer
        ret = sch_mux_receive(mux->sch, of->index, mt.pkt);
        stream_idx = mt.pkt->stream_index;

        if (stream_idx < 0) {
            // EOF - all streams finished
            av_log(mux, AV_LOG_VERBOSE, "All streams finished\n");
            ret = 0;
            break;
        }

        ost = of->streams[mux->sch_stream_idx[stream_idx]];
        mt.pkt->stream_index = ost->index;

        // Process packet (stream copy or encode)
        ret = mux_packet_filter(mux, &mt, ost, ret < 0 ? NULL : mt.pkt, &stream_eof);
        av_packet_unref(mt.pkt);

        if (ret == AVERROR_EOF) {
            if (stream_eof) {
                sch_mux_receive_finish(mux->sch, of->index, stream_idx);
            } else {
                av_log(mux, AV_LOG_VERBOSE, "Muxer returned EOF\n");
                ret = 0;
                break;
            }
        } else if (ret < 0) {
            av_log(mux, AV_LOG_ERROR, "Error muxing a packet\n");
            break;
        }
    }

    mux_thread_uninit(&mt);
    return ret;
}
```

---

## 5. Stream Copy Logic: `of_streamcopy()`

**Location:** `fftools/ffmpeg_mux.c:460`

```c
static int of_streamcopy(OutputFile *of, OutputStream *ost, AVPacket *pkt)
{
    MuxStream *ms = ms_from_ost(ost);
    FrameData *fd = pkt->opaque_ref ? (FrameData*)pkt->opaque_ref->data : NULL;
    int64_t dts = fd ? fd->dts_est : AV_NOPTS_VALUE;
    int64_t start_time = (of->start_time == AV_NOPTS_VALUE) ? 0 : of->start_time;
    int64_t ts_offset;

    // Check duration limit (-t)
    if (of->recording_time != INT64_MAX &&
        dts >= of->recording_time + start_time)
        return AVERROR_EOF;  // Stop when duration exceeded

    // Skip non-keyframes before start (if configured)
    if (!ms->streamcopy_started && !(pkt->flags & AV_PKT_FLAG_KEY) &&
        !ms->copy_initial_nonkeyframes)
        return AVERROR(EAGAIN);  // Skip until first keyframe

    if (!ms->streamcopy_started) {
        if (!ms->copy_prior_start &&
            (pkt->pts == AV_NOPTS_VALUE ?
             dts < ms->ts_copy_start :
             pkt->pts < ms->ts_copy_start))
            return AVERROR(EAGAIN);

        ms->streamcopy_started = 1;
    }

    return 0;
}
```

---

## 6. ThreadQueue Structure

```c
ThreadQueue {
    nb_streams: 2          // Number of streams (video + audio)
    finished[]: [0, 0]     // FINISHED_SEND | FINISHED_RECV flags
    cond, lock: pthread    // Synchronization
    fifo: ContainerFIFO    // Thread-safe packet buffer
}
```

### `tq_receive()` Implementation

```c
// ffmpeg_sched.c:193 (thread_queue.c)
int tq_receive(ThreadQueue *tq, int *stream_idx, void *data)
{
    int ret;

    *stream_idx = -1;

    pthread_mutex_lock(&tq->lock);

    while (1) {
        size_t can_read = av_container_fifo_can_read(tq->fifo);

        ret = receive_locked(tq, stream_idx, data);

        // signal other threads if the fifo state changed
        if (can_read != av_container_fifo_can_read(tq->fifo))
            pthread_cond_broadcast(&tq->cond);

        if (ret == AVERROR(EAGAIN)) {
            pthread_cond_wait(&tq->cond, &tq->lock);  // BLOCK HERE
            continue;
        }

        break;
    }

    pthread_mutex_unlock(&tq->lock);

    return ret;
}
```

---

## 7. EOF Handling

When demuxer reaches EOF:

```c
// ffmpeg_demux.c:715-739
if (ret == AVERROR_EOF) {
    // Flush bitstream filters
    ret_bsf = demux_bsf_flush(d, &dt);

    if (d->loop) {
        // Signal looping
        dt.pkt_demux->stream_index = -1;
        sch_demux_send(d->sch, f->index, dt.pkt_demux, 0);
        seek_to_start(d, ...);
        continue;
    }

    // Send EOF to all destinations
    demux_send_for_stream(sch, d, &d->streams[i], NULL, 0);
}
```

This calls `send_to_mux()` with `pkt = NULL`:

```c
tq_send_finish(mux->queue, stream_idx);  // Set FINISHED_SEND flag
```

When muxer receives `stream_idx = -1`, it knows all streams are done and exits.

---

## 8. Key Features of `-c copy` Path

| Feature | Implementation |
|---------|----------------|
| `-ss 0` | `avformat_seek_file()` in `ifile_open()` |
| `-t 7` | `of_streamcopy()` checks `recording_time` |
| `-map 0` | `create_streams()` matches all input→output |
| `-c copy` | No encoder/decoder; packets go demuxer→muxer directly |

---

## 9. Key Data Structures

| Structure | File | Purpose |
|-----------|------|---------|
| `Demuxer` | ffmpeg_demux.c | Manages input file, threads |
| `DemuxStream` | ffmpeg_demux.c | Per-stream demuxer state |
| `Muxer` | ffmpeg_mux.c | Manages output file, threads |
| `MuxStream` | ffmpeg_mux.c | Per-stream muxer state |
| `Scheduler` | ffmpeg_sched.c | Manages demux/mux/enc threads |
| `InputStream` | ffmpeg.h | Input stream (decoded packets) |
| `OutputStream` | ffmpeg.h | Output stream (to be muxed) |
| `ThreadQueue` | thread_queue.c | Thread-safe packet queue |
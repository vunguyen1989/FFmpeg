/*
 * Cut a video segment from t1 to t2 using stream copy (no reencoding)
 *
 * Usage:    ./cut_video <input_file> <start_time> <end_time> <output_file>
 * Example:  ./cut_video input.mp4 5 15 output.mp4
 *
 * This extracts seconds 5-15 from input.mp4 and writes to output.mp4
 *
 * Flow chart:
 * +-------------------+
 * | avformat_alloc_context()         -- allocate input AVFormatContext
 * +-------------------+
 *           |
 *           v
 * +-------------------+
 * | avformat_open_input()            -- open input file
 * +-------------------+
 *           |
 *           v
 * +-------------------+
 * | avformat_alloc_output_context2() -- allocate output AVFormatContext
 * +-------------------+
 *           |
 *           v
 * +-----------------------+
 * | avformat_new_stream()           -- create output streams
 * +-----------------------+
 *           |
 *           v
 * +-----------------------+
 * | avio_open()                      -- open output file
 * +-----------------------+
 *           |
 *           v
 * +-----------------------------+
 * | avformat_write_header()              -- write output header
 * +-----------------------------+
 *           |
 *           v
 * +-----------------------------------+
 * | av_seek_frame(t1)                        -- seek to start time
 * +-----------------------------------+
 *           |
 *           v
 * +-----------------------------+
 * | while av_read_frame():      -- read input packets
 * |   if pts >= t1 && pts < t2: |
 * |     adjust timestamps       |
 * |     av_interleaved_write_frame()  -- write packet
 * +-----------------------------+
 *           |
 *           v
 * +-----------------------------+
 * | av_write_trailer()          -- write output trailer
 * +-----------------------------+
 *           |
 *           v
 * +------------------------------+
 * | cleanup: avio_close, avformat_close_input,
 * |   avformat_free_context
 * +------------------------------+
 *
 * libs using:
 *   - libavformat  (avformat.h)    -- format: open, seek, read/write
 *   - libavcodec   (avcodec.h)     -- for copy codec parameters
 *   - libavutil    (avutil.h)      -- utilities: av_err2str, time base
 */

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void logging(const char *fmt, ...);

int main(int argc, const char *argv[])
{
    if (argc != 5) {
        printf("Usage: %s <input_file> <start_time> <end_time> <output_file>\n", argv[0]);
        printf("Example: %s input.mp4 5 15 output.mp4\n", argv[0]);
        return -1;
    }

    const char *input_file = argv[1];
    double start_time = atof(argv[2]);   // Start time in seconds
    double end_time = atof(argv[3]);     // End time in seconds
    const char *output_file = argv[4];

    logging("Input file: %s", input_file);
    logging("Time range: %.2f to %.2f seconds", start_time, end_time);
    logging("Output file: %s", output_file);

    // ============================================
    // Step 1: Open input file
    // ============================================
    AVFormatContext *ifmt_ctx = NULL;
    if (avformat_open_input(&ifmt_ctx, input_file, NULL, NULL) != 0) {
        logging("ERROR: Could not open input file");
        return -1;
    }

    // Get stream info
    if (avformat_find_stream_info(ifmt_ctx, NULL) < 0) {
        logging("ERROR: Could not get stream info");
        avformat_close_input(&ifmt_ctx);
        return -1;
    }

    logging("Input format: %s, duration: %.2f seconds",
            ifmt_ctx->iformat->name,
            ifmt_ctx->duration / (double)AV_TIME_BASE);

    // Print stream info
    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *st = ifmt_ctx->streams[i];
        logging("Stream %d: %s, time_base=%d/%d",
                i,
                av_get_media_type_string(st->codecpar->codec_type),
                st->time_base.num, st->time_base.den);
    }

    // ============================================
    // Step 2: Create output context
    // ============================================
    AVFormatContext *ofmt_ctx = NULL;
    if (avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, output_file) < 0) {
        logging("ERROR: Could not create output context");
        avformat_close_input(&ifmt_ctx);
        return -1;
    }

    // Copy streams from input to output
    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, NULL);

        if (!out_stream) {
            logging("ERROR: Failed to allocate output stream");
            avformat_free_context(ofmt_ctx);
            avformat_close_input(&ifmt_ctx);
            return -1;
        }

        // Copy codec parameters from input to output
        if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0) {
            logging("ERROR: Failed to copy codec parameters");
            avformat_free_context(ofmt_ctx);
            avformat_close_input(&ifmt_ctx);
            return -1;
        }

        out_stream->codecpar->codec_tag = 0;
        out_stream->time_base = in_stream->time_base;

        logging("Created output stream %d: %s", i,
                av_get_media_type_string(out_stream->codecpar->codec_type));
    }

    // Open output file for writing
    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&ofmt_ctx->pb, output_file, AVIO_FLAG_WRITE) < 0) {
            logging("ERROR: Could not open output file");
            avformat_free_context(ofmt_ctx);
            avformat_close_input(&ifmt_ctx);
            return -1;
        }
    }

    // Write output file header
    if (avformat_write_header(ofmt_ctx, NULL) < 0) {
        logging("ERROR: Could not write output header");
        avio_closep(&ofmt_ctx->pb);
        avformat_free_context(ofmt_ctx);
        avformat_close_input(&ifmt_ctx);
        return -1;
    }

    logging("Output header written successfully");

    // ============================================
    // Step 3: Seek to start time
    // ============================================
    int64_t seek_start = (int64_t)(start_time * AV_TIME_BASE);

    // Seek to start time (AVSEEK_FLAG_BACKWARD seeks to nearest keyframe before time)
    if (av_seek_frame(ifmt_ctx, -1, seek_start, AVSEEK_FLAG_BACKWARD) < 0) {
        logging("Warning: Could not seek to start time %.2f", start_time);
    } else {
        logging("Seeked to %.2f seconds (nearest keyframe before)", start_time);
    }

    // ============================================
    // Step 4: Read packets and write to output
    // ============================================
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        logging("ERROR: Could not allocate packet");
        avio_closep(&ofmt_ctx->pb);
        avformat_free_context(ofmt_ctx);
        avformat_close_input(&ifmt_ctx);
        return -1;
    }

    int64_t pts_start = (int64_t)(start_time * AV_TIME_BASE);
    int64_t pts_end = (int64_t)(end_time * AV_TIME_BASE);
    int64_t first_pts = AV_NOPTS_VALUE;
    int64_t first_dts = AV_NOPTS_VALUE;
    int packet_count = 0;

    logging("Starting packet processing...");

    while (av_read_frame(ifmt_ctx, pkt) >= 0) {
        // Get packet timestamps
        int64_t pts = pkt->pts != AV_NOPTS_VALUE ?
                      av_rescale_q(pkt->pts, ifmt_ctx->streams[pkt->stream_index]->time_base, AV_TIME_BASE_Q) :
                      AV_NOPTS_VALUE;
        int64_t dts = pkt->dts != AV_NOPTS_VALUE ?
                      av_rescale_q(pkt->dts, ifmt_ctx->streams[pkt->stream_index]->time_base, AV_TIME_BASE_Q) :
                      AV_NOPTS_VALUE;

        // Check if packet is within time range
        if (pts != AV_NOPTS_VALUE && pts >= pts_end) {
            logging("Reached end time (pts=%lld >= %lld)", pts, pts_end);
            av_packet_unref(pkt);
            break;
        }

        // Skip packets before start time
        if (pts != AV_NOPTS_VALUE && pts < pts_start) {
            av_packet_unref(pkt);
            continue;
        }

        // Record first PTS/DTS for offset calculation
        if (first_pts == AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE) {
            first_pts = pkt->pts;
            first_dts = pkt->dts;
            logging("First packet: stream=%d, pts=%lld, dts=%lld",
                    pkt->stream_index, first_pts, first_dts);
        }

        // Adjust timestamps: subtract offset so output starts from 0
        if (first_pts != AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE) {
            pkt->pts -= first_pts;
        }
        if (first_dts != AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE) {
            pkt->dts -= first_dts;
        }

        // Rescale timestamps to output stream time base
        AVStream *in_stream = ifmt_ctx->streams[pkt->stream_index];
        AVStream *out_stream = ofmt_ctx->streams[pkt->stream_index];

        // Convert from input time base to output time base
        av_packet_rescale_ts(pkt, in_stream->time_base, out_stream->time_base);

        // Set duration in output time base
        pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);

        logging("Packet %d: stream=%d, pts=%lld, dts=%lld, size=%d, flags=%d",
                ++packet_count,
                pkt->stream_index,
                pkt->pts,
                pkt->dts,
                pkt->size,
                (pkt->flags & AV_PKT_FLAG_KEY) ? 1 : 0);

        // Write packet to output
        if (av_interleaved_write_frame(ofmt_ctx, pkt) < 0) {
            logging("ERROR: Failed to write packet");
            av_packet_unref(pkt);
            break;
        }

        av_packet_unref(pkt);
    }

    logging("Total packets written: %d", packet_count);

    // ============================================
    // Step 5: Write trailer and cleanup
    // ============================================
    if (av_write_trailer(ofmt_ctx) < 0) {
        logging("ERROR: Could not write trailer");
    } else {
        logging("Output trailer written successfully");
    }

    // Cleanup
    av_packet_free(&pkt);
    avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
    avformat_close_input(&ifmt_ctx);

    logging("Done! Output saved to %s", output_file);
    return 0;
}

static void logging(const char *fmt, ...)
{
    va_list args;
    fprintf(stderr, "LOG: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}
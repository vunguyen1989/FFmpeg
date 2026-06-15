/**
 *
 *   File:   app.c
 *           Video player application - mimics tutorials 01-07
 *           Display video to screen using SDL2
 *
 *   Author: Rambod Rahmani <rambodrahmani@autistici.org>
 *           Created on 8/6/18.
 *
 **/

#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

/**
 * PacketQueue Structure Declaration.
 */
typedef struct PacketQueue
{
    AVPacketList * first_pkt;
    AVPacketList * last_pkt;
    int nb_packets;
    int size;
    SDL_mutex * mutex;
    SDL_cond * cond;
} PacketQueue;

// audio PacketQueue instance
PacketQueue audioq;

// global quit flag
int quit = 0;

void printHelpMenu();

void packet_queue_init(PacketQueue * q);
int packet_queue_put(PacketQueue * queue, AVPacket * packet);
static int packet_queue_get(PacketQueue * q, AVPacket * pkt, int block);
void audio_callback(void * userdata, Uint8 * stream, int len);
int audio_decode_frame(AVCodecContext * aCodecCtx, uint8_t * audio_buf, int buf_size);
static int audio_resampling(
    AVCodecContext * audio_decode_ctx,
    AVFrame * decoded_audio_frame,
    enum AVSampleFormat out_sample_fmt,
    int out_channels,
    int out_sample_rate,
    uint8_t * out_buf
);

/**
 * Initialize the given PacketQueue.
 */
void packet_queue_init(PacketQueue * q)
{
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}

/**
 * Put the given AVPacket in the given PacketQueue.
 */
int packet_queue_put(PacketQueue * q, AVPacket * pkt)
{
    AVPacketList * avPacketList;
    avPacketList = av_malloc(sizeof(AVPacketList));
    if (!avPacketList) return -1;

    av_packet_ref(&avPacketList->pkt, pkt);
    avPacketList->next = NULL;

    SDL_LockMutex(q->mutex);

    if (!q->last_pkt)
    {
        q->first_pkt = avPacketList;
    }
    else
    {
        q->last_pkt->next = avPacketList;
    }
    q->last_pkt = avPacketList;

    q->nb_packets++;
    q->size += avPacketList->pkt.size;

    SDL_CondSignal(q->cond);
    SDL_UnlockMutex(q->mutex);

    return 0;
}

/**
 * Get the first AVPacket from the given PacketQueue.
 */
static int packet_queue_get(PacketQueue * q, AVPacket * pkt, int block)
{
    int ret;
    AVPacketList * avPacketList;

    SDL_LockMutex(q->mutex);

    for (;;)
    {
        if (quit)
        {
            ret = -1;
            break;
        }

        avPacketList = q->first_pkt;
        if (avPacketList)
        {
            q->first_pkt = avPacketList->next;
            if (!q->first_pkt)
            {
                q->last_pkt = NULL;
            }
            q->nb_packets--;
            q->size -= avPacketList->pkt.size;
            *pkt = avPacketList->pkt;
            av_free(avPacketList);
            ret = 1;
            break;
        }
        else if (!block)
        {
            ret = 0;
            break;
        }
        else
        {
            SDL_CondWait(q->cond, q->mutex);
        }
    }

    SDL_UnlockMutex(q->mutex);
    return ret;
}

/**
 * Entry point.
 *
 * @param   argc    command line arguments counter.
 * @param   argv    command line arguments.
 *
 * @return          execution exit code.
 */
int main(int argc, char * argv[])
{
    // SDL initialization
    printf("Debug: Initializing SDL...\n");
    int ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
    if (ret != 0)
    {
        printf("Could not initialize SDL - %s\n.", SDL_GetError());
        return -1;
    }
    printf("Debug: SDL initialized.\n");

    // we get our filename from the first argument, check if the file name is
    // provided, show help menu if not
    if ( !(argc > 2) )
    {
        printHelpMenu();
        return -1;
    }

    // declare the AVFormatContext
    AVFormatContext * pFormatCtx = NULL;

    // now we can actually open the file:
    printf("Debug: Opening file %s...\n", argv[1]);
    ret = avformat_open_input(&pFormatCtx, argv[1], NULL, NULL);
    if (ret < 0)
    {
        printf("Could not open file %s\n", argv[1]);
        return -1;
    }
    printf("Debug: File opened.\n");

    // Retrieve stream information
    ret = avformat_find_stream_info(pFormatCtx, NULL);
    if (ret < 0)
    {
        printf("Could not find stream information %s\n", argv[1]);
        return -1;
    }

    // Dump information about file onto standard error
    av_dump_format(pFormatCtx, 0, argv[1], 0);

    // Find the first video and audio stream
    int i;
    AVCodecContext * pCodecCtx = NULL;
    int videoStream = -1;
    int audioStream = -1;
    for (i = 0; i < (int)pFormatCtx->nb_streams; i++)
    {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStream < 0)
        {
            videoStream = i;
        }
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStream < 0)
        {
            audioStream = i;
        }
    }

    if (videoStream == -1)
    {
        printf("Could not find video stream.\n");
        return -1;
    }

    if (audioStream == -1)
    {
        printf("Could not find audio stream.\n");
        return -1;
    }

    // Find the decoder for the video stream
    const AVCodec * pCodec = NULL;
    pCodec = avcodec_find_decoder(pFormatCtx->streams[videoStream]->codecpar->codec_id);
    if (pCodec == NULL)
    {
        printf("Unsupported codec!\n");
        return -1;
    }

    pCodecCtx = avcodec_alloc_context3(pCodec);
    ret = avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar);
    if (ret != 0)
    {
        printf("Could not copy codec context.\n");
        return -1;
    }

    // Open codec
    printf("Debug: Opening codec...\n");
    ret = avcodec_open2(pCodecCtx, pCodec, NULL);
    if (ret < 0)
    {
        printf("Could not open codec.\n");
        return -1;
    }
    printf("Debug: Codec opened.\n");

    // Audio: retrieve audio codec
    AVCodecContext * aCodecCtx = NULL;
    const AVCodec * aCodec = NULL;
    aCodec = avcodec_find_decoder(pFormatCtx->streams[audioStream]->codecpar->codec_id);
    if (aCodec == NULL)
    {
        printf("Unsupported audio codec!\n");
        return -1;
    }

    // Audio: retrieve audio codec context
    aCodecCtx = avcodec_alloc_context3(aCodec);
    ret = avcodec_parameters_to_context(aCodecCtx, pFormatCtx->streams[audioStream]->codecpar);
    if (ret != 0)
    {
        printf("Could not copy audio codec context.\n");
        return -1;
    }

    // Ensure channel layout is properly initialized
    if (aCodecCtx->ch_layout.nb_channels <= 0) {
        av_channel_layout_default(&aCodecCtx->ch_layout, 2);
    }

    // Audio: SDL audio specs
    SDL_AudioSpec wanted_specs;
    SDL_AudioSpec specs;
    wanted_specs.freq = aCodecCtx->sample_rate;
    wanted_specs.format = AUDIO_S16SYS;
    wanted_specs.channels = aCodecCtx->ch_layout.nb_channels;
    wanted_specs.silence = 0;
    wanted_specs.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_specs.callback = audio_callback;
    wanted_specs.userdata = aCodecCtx;

    SDL_AudioDeviceID audioDeviceID;
    audioDeviceID = SDL_OpenAudioDevice(NULL, 0, &wanted_specs, &specs, SDL_AUDIO_ALLOW_FORMAT_CHANGE);
    if (audioDeviceID == 0)
    {
        printf("Failed to open audio device: %s.\n", SDL_GetError());
        return -1;
    }

    // Audio: initialize the audio AVCodecContext to use the given audio AVCodec
    ret = avcodec_open2(aCodecCtx, aCodec, NULL);
    if (ret < 0)
    {
        printf("Could not open audio codec.\n");
        return -1;
    }

    // Audio: init audio PacketQueue and start playing
    packet_queue_init(&audioq);
    SDL_PauseAudioDevice(audioDeviceID, 0);

    // Allocate video frame
    AVFrame * pFrame = NULL;
    pFrame = av_frame_alloc();
    if (pFrame == NULL)
    {
        printf("Could not allocate frame.\n");
        return -1;
    }

    // Create a window with the specified position, dimensions, and flags.
    SDL_Window * screen = SDL_CreateWindow(
                            "SDL Video Player",
                            SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED,
                            pCodecCtx->width/2,
                            pCodecCtx->height/2,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (!screen)
    {
        printf("SDL: could not set video mode - exiting.\n");
        return -1;
    }
    printf("Debug: SDL window created.\n");

    SDL_GL_SetSwapInterval(1);

    // Create a 2D rendering context for a window
    SDL_Renderer * renderer = NULL;
    renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!renderer)
    {
        printf("SDL: could not create renderer - exiting.\n");
        return -1;
    }

    // Create a texture for a rendering context
    SDL_Texture * texture = NULL;
    texture = SDL_CreateTexture(
                renderer,
                SDL_PIXELFORMAT_YV12,
                SDL_TEXTUREACCESS_STREAMING,
                pCodecCtx->width,
                pCodecCtx->height
            );

    // Initialize SWS context for software scaling to YUV420P
    struct SwsContext * sws_ctx = NULL;
    sws_ctx = sws_getContext(
        pCodecCtx->width,
        pCodecCtx->height,
        pCodecCtx->pix_fmt,
        pCodecCtx->width,
        pCodecCtx->height,
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL
    );

    // Allocate packet
    AVPacket * pPacket = av_packet_alloc();
    if (pPacket == NULL)
    {
        printf("Could not alloc packet,\n");
        return -1;
    }

    // Allocate buffer for YUV420P
    int numBytes;
    uint8_t * buffer = NULL;
    numBytes = av_image_get_buffer_size(
                AV_PIX_FMT_YUV420P,
                pCodecCtx->width,
                pCodecCtx->height,
                32
            );
    buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));

    // Allocate picture frame for YUV420P
    AVFrame * pict = av_frame_alloc();
    av_image_fill_arrays(
        pict->data,
        pict->linesize,
        buffer,
        AV_PIX_FMT_YUV420P,
        pCodecCtx->width,
        pCodecCtx->height,
        32
    );

    // Get max frames to decode from command line
    int maxFramesToDecode;
    sscanf (argv[2], "%d", &maxFramesToDecode);

    // SDL event for handling quit
    SDL_Event event;

    // Read frames from video
    i = 0;
    while (av_read_frame(pFormatCtx, pPacket) >= 0)
    {
        // Is this a packet from the video stream?
        if (pPacket->stream_index == videoStream)
        {
            // Decode video frame
            ret = avcodec_send_packet(pCodecCtx, pPacket);
            if (ret < 0)
            {
                printf("Error sending packet for decoding.\n");
                return -1;
            }

            while (ret >= 0)
            {
                ret = avcodec_receive_frame(pCodecCtx, pFrame);

                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                {
                    break;
                }
                else if (ret < 0)
                {
                    printf("Error while decoding.\n");
                    return -1;
                }

                // Convert the image from its native format to YUV420P
                sws_scale(
                    sws_ctx,
                    (uint8_t const * const *)pFrame->data,
                    pFrame->linesize,
                    0,
                    pCodecCtx->height,
                    pict->data,
                    pict->linesize
                );

                if (++i <= maxFramesToDecode)
                {
                    // get clip fps
                    double fps = av_q2d(pFormatCtx->streams[videoStream]->r_frame_rate);

                    // get clip sleep time
                    double sleep_time = 1.0/(double)fps;

                    // Sleep to maintain video framerate
                    SDL_Delay((1000 * sleep_time) - 10);

                    // SDL_Rect for positioning
                    SDL_Rect rect;
                    rect.x = 0;
                    rect.y = 0;
                    rect.w = pCodecCtx->width;
                    rect.h = pCodecCtx->height;

                    printf(
                        "Frame %c (%lld) pts %lld dts %lld [%dx%d]\n",
                        av_get_picture_type_char(pFrame->pict_type),
                        (long long)pCodecCtx->frame_num,
                        (long long)pFrame->pts,
                        (long long)pFrame->pkt_dts,
                        pCodecCtx->width,
                        pCodecCtx->height
                    );

                    // Update texture with YUV data
                    SDL_UpdateYUVTexture(
                        texture,
                        &rect,
                        pict->data[0],
                        pict->linesize[0],
                        pict->data[1],
                        pict->linesize[1],
                        pict->data[2],
                        pict->linesize[2]
                    );

                    // Clear and render
                    SDL_RenderClear(renderer);
                    SDL_RenderCopy(renderer, texture, NULL, NULL);
                    SDL_RenderPresent(renderer);
                }
                else
                {
                    break;
                }
            }

            if (i > maxFramesToDecode)
            {
                break;
            }
        }
        // Is this a packet from the audio stream?
        else if (pPacket->stream_index == audioStream)
        {
            // put the AVPacket in the audio PacketQueue
            packet_queue_put(&audioq, pPacket);
        }
        else
        {
            // wipe the packet
            av_packet_unref(pPacket);
        }

        // Free the packet
        av_packet_unref(pPacket);

        // Handle SDL quit event
        SDL_PollEvent(&event);
        switch(event.type)
        {
            case SDL_QUIT:
            {
                quit = 1;
                SDL_Quit();
                exit(0);
            }
            break;

            default:
            {
                // nothing to do
            }
            break;
        }
    }

    /**
     * Cleanup.
     */

    // Free the YUV frame
    av_frame_free(&pFrame);
    av_free(pFrame);

    av_free(buffer);

    // Close the codecs
    avcodec_free_context(&pCodecCtx);
    avcodec_free_context(&aCodecCtx);

    // Close the video file
    avformat_close_input(&pFormatCtx);

    // Cleanup SDL
    SDL_DestroyRenderer(renderer);
    SDL_Quit();

    return 0;
}

/**
 * Pull in data from audio_decode_frame(), store the result in an intermediary
 * buffer, attempt to write as many bytes as the amount defined by len to
 * stream, and get more data if we don't have enough yet.
 */
void audio_callback(void * userdata, Uint8 * stream, int len)
{
    AVCodecContext * aCodecCtx = (AVCodecContext *) userdata;
    int len1 = -1;
    int audio_size = -1;

    static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
    static unsigned int audio_buf_size = 0;
    static unsigned int audio_buf_index = 0;

    while (len > 0)
    {
        if (quit) return;

        if (audio_buf_index >= audio_buf_size)
        {
            audio_size = audio_decode_frame(aCodecCtx, audio_buf, sizeof(audio_buf));
            if (audio_size < 0)
            {
                audio_buf_size = 1024;
                memset(audio_buf, 0, audio_buf_size);
            }
            else
            {
                audio_buf_size = audio_size;
            }
            audio_buf_index = 0;
        }

        len1 = audio_buf_size - audio_buf_index;
        if (len1 > len) len1 = len;

        memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
        len -= len1;
        stream += len1;
        audio_buf_index += len1;
    }
}

/**
 * Get a packet from the queue if available. Decode the extracted packet.
 * Once we have the frame, resample it and copy to audio buffer.
 */
int audio_decode_frame(AVCodecContext * aCodecCtx, uint8_t * audio_buf, int buf_size)
{
    (void)buf_size;
    AVPacket * avPacket = av_packet_alloc();
    static uint8_t * audio_pkt_data = NULL;
    (void)audio_pkt_data;
    static int audio_pkt_size = 0;

    static AVFrame * avFrame = NULL;
    avFrame = av_frame_alloc();
    if (!avFrame) return -1;

    int len1 = 0;
    int data_size = 0;

    for (;;)
    {
        if (quit) return -1;

        while (audio_pkt_size > 0)
        {
            int ret = avcodec_receive_frame(aCodecCtx, avFrame);
            if (ret == 0) {}
            if (ret == AVERROR(EAGAIN)) ret = 0;
            if (ret == 0) ret = avcodec_send_packet(aCodecCtx, avPacket);
            if (ret == AVERROR(EAGAIN)) ret = 0;
            else if (ret < 0) return -1;
            else len1 = avPacket->size;

            if (len1 < 0)
            {
                audio_pkt_size = 0;
                break;
            }

            audio_pkt_data += len1;
            audio_pkt_size -= len1;
            data_size = 0;

            if (ret >= 0)
            {
                data_size = audio_resampling(
                    aCodecCtx,
                    avFrame,
                    AV_SAMPLE_FMT_S16,
                    aCodecCtx->ch_layout.nb_channels,
                    aCodecCtx->sample_rate,
                    audio_buf
                );
            }

            if (data_size <= 0) continue;
            return data_size;
        }

        if (avPacket->data) av_packet_unref(avPacket);

        int ret = packet_queue_get(&audioq, avPacket, 1);
        if (ret < 0) return -1;

        audio_pkt_data = avPacket->data;
        audio_pkt_size = avPacket->size;
    }

    return 0;
}

/**
 * Resample the audio data retrieved using FFmpeg before playing it.
 */
static int audio_resampling(
    AVCodecContext * audio_decode_ctx,
    AVFrame * decoded_audio_frame,
    enum AVSampleFormat out_sample_fmt,
    int out_channels,
    int out_sample_rate,
    uint8_t * out_buf)
{
    if (quit) return -1;

    SwrContext * swr_ctx = NULL;
    int ret = 0;
    int64_t in_channel_layout = audio_decode_ctx->ch_layout.u.mask;
    int64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
    int out_nb_channels = 0;
    int out_linesize = 0;
    int in_nb_samples = 0;
    int out_nb_samples = 0;
    int max_out_nb_samples = 0;
    uint8_t ** resampled_data = NULL;
    int resampled_data_size = 0;

    swr_ctx = swr_alloc();
    if (!swr_ctx) return -1;

    in_channel_layout = audio_decode_ctx->ch_layout.u.mask;
    if (!av_channel_layout_check(&audio_decode_ctx->ch_layout) || in_channel_layout <= 0) {
        av_channel_layout_default(&audio_decode_ctx->ch_layout, 2);
        in_channel_layout = audio_decode_ctx->ch_layout.u.mask;
    }

    if (in_channel_layout <= 0) return -1;

    if (out_channels == 1) out_channel_layout = AV_CH_LAYOUT_MONO;
    else if (out_channels == 2) out_channel_layout = AV_CH_LAYOUT_STEREO;
    else out_channel_layout = AV_CH_LAYOUT_SURROUND;

    in_nb_samples = decoded_audio_frame->nb_samples;
    if (in_nb_samples <= 0) return -1;

    av_opt_set_int(swr_ctx, "in_channel_layout", in_channel_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", audio_decode_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", audio_decode_ctx->sample_fmt, 0);
    av_opt_set_int(swr_ctx, "out_channel_layout", out_channel_layout, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", out_sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", out_sample_fmt, 0);

    ret = swr_init(swr_ctx);
    if (ret < 0) return -1;

    max_out_nb_samples = out_nb_samples = av_rescale_rnd(
        in_nb_samples, out_sample_rate, audio_decode_ctx->sample_rate, AV_ROUND_UP);

    if (max_out_nb_samples <= 0) return -1;

    out_nb_channels = out_channels;

    ret = av_samples_alloc_array_and_samples(
        &resampled_data, &out_linesize, out_nb_channels,
        out_nb_samples, out_sample_fmt, 0);

    if (ret < 0) return -1;

    out_nb_samples = av_rescale_rnd(
        swr_get_delay(swr_ctx, audio_decode_ctx->sample_rate) + in_nb_samples,
        out_sample_rate, audio_decode_ctx->sample_rate, AV_ROUND_UP);

    if (out_nb_samples <= 0) return -1;

    if (out_nb_samples > max_out_nb_samples)
    {
        av_free(resampled_data[0]);
        av_samples_alloc(resampled_data, &out_linesize, out_nb_channels,
            out_nb_samples, out_sample_fmt, 1);
        max_out_nb_samples = out_nb_samples;
    }

    if (swr_ctx)
    {
        ret = swr_convert(swr_ctx, resampled_data, out_nb_samples,
            (const uint8_t **)decoded_audio_frame->data, decoded_audio_frame->nb_samples);

        if (ret < 0) return -1;

        resampled_data_size = av_samples_get_buffer_size(
            &out_linesize, out_nb_channels, ret, out_sample_fmt, 1);

        if (resampled_data_size < 0) return -1;
    }
    else
    {
        return -1;
    }

    memcpy(out_buf, resampled_data[0], resampled_data_size);

    if (resampled_data) av_freep(&resampled_data[0]);
    av_freep(&resampled_data);
    if (swr_ctx) swr_free(&swr_ctx);

    return resampled_data_size;
}

/**
 * Print help menu containing usage information.
 */
void printHelpMenu()
{
    printf("Invalid arguments.\n\n");
    printf("Usage: ./app  <max-frames-to-decode>\n\n");
    printf("e.g: ./app video.mp4 200\n");
}
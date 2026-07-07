#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define MKDIR(path) _mkdir(path)
#else
#define MKDIR(path) mkdir(path, 0755)
#endif

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
#include <png.h>

/* ──────────────────── i18n ──────────────────── */

static int g_lang = 0; /* 0 = English, 1 = Chinese */

enum {
    S_TITLE, S_USAGE, S_OPTIONS, S_OPT_I, S_OPT_S, S_OPT_S2,
    S_EXAMPLES, S_ERR_INTERVAL, S_ERR_UNKNOWN, S_ERR_NO_INPUT,
    S_OUT_DIR, S_ERR_MKDIR, S_ERR_OPEN_VIDEO, S_ERR_STREAM_INFO,
    S_ERR_NO_VIDEO, S_ERR_CODEC, S_ERR_DECODER,
    S_VIDEO_INFO, S_CAPTURING, S_DONE, S_ERR_INTERVAL_FMT,
    S_EX1, S_EX2, S_EX3, S_FRAME_FMT,
    S_COUNT
};

static const char *S_en[S_COUNT] = {
    /* S_TITLE       */ "Kohane - Video Frame Extractor\n\n",
    /* S_USAGE       */ "Usage: Kohane -i <video> [-s <interval>]\n\n",
    /* S_OPTIONS     */ "Options:\n",
    /* S_OPT_I       */ "  -i <file>     Input video file path\n",
    /* S_OPT_S       */ "  -s <interval> Capture interval (default: 30s)\n",
    /* S_OPT_S2      */ "                Supports: 30s, 2m, 1h, 500ms, 1d, etc.\n",
    /* S_EXAMPLES    */ "\nExamples:\n",
    /* S_ERR_INTERVAL*/ "Error: invalid interval format\n",
    /* S_ERR_UNKNOWN */ "Unknown argument\n",
    /* S_ERR_NO_INPUT*/ "Error: please specify input file with -i\n",
    /* S_OUT_DIR     */ "Output directory: %s\n",
    /* S_ERR_MKDIR   */ "Error: cannot create directory '%s'\n",
    /* S_ERR_OPEN_VID*/ "Error: cannot open video '%s'\n",
    /* S_ERR_STREAM  */ "Error: cannot retrieve stream info\n",
    /* S_ERR_NO_VIDEO*/ "Error: no video stream found\n",
    /* S_ERR_CODEC   */ "Error: unsupported codec\n",
    /* S_ERR_DECODER */ "Error: cannot open decoder\n",
    /* S_VIDEO_INFO  */ "Video: %dx%d, interval: %.2f sec\n",
    /* S_CAPTURING   */ "Capturing...\n",
    /* S_DONE        */ "\nDone! %d frame(s) captured -> %s\n",
    /* S_ERR_INT_FMT */ "Error: invalid interval format '%s'\n",
    /* S_EX1         */ "  Kohane -i video.mp4\n",
    /* S_EX2         */ "  Kohane -i video.mp4 -s 1m\n",
    /* S_EX3         */ "  Kohane -i video.mp4 -s 500ms\n",
    /* S_FRAME_FMT   */ "  [%04d] %s  (%.2fs)\n",
};

static const char *S_zh[S_COUNT] = {
    /* S_TITLE       */ "Kohane - \xe8\xa7\x86\xe9\xa2\x91\xe5\xb8\xa7\xe6\x88\xaa\xe5\x8f\x96\xe5\xb7\xa5\xe5\x85\xb7\n\n",
    /* S_USAGE       */ "\xe7\x94\xa8\xe6\xb3\x95: Kohane -i <\xe8\xa7\x86\xe9\xa2\x91\xe6\x96\x87\xe4\xbb\xb6> [-s <\xe9\x97\xb4\xe9\x9a\x94>]\n\n",
    /* S_OPTIONS     */ "\xe9\x80\x89\xe9\xa1\xb9:\n",
    /* S_OPT_I       */ "  -i <\xe6\x96\x87\xe4\xbb\xb6>   \xe8\xbe\x93\xe5\x85\xa5\xe8\xa7\x86\xe9\xa2\x91\xe6\x96\x87\xe4\xbb\xb6\xe8\xb7\xaf\xe5\xbe\x84\n",
    /* S_OPT_S       */ "  -s <\xe9\x97\xb4\xe9\x9a\x94>   \xe6\x88\xaa\xe5\x9b\xbe\xe9\x97\xb4\xe9\x9a\x94 (\xe9\xbb\x98\xe8\xae\xa4: 30s)\n",
    /* S_OPT_S2      */ "              \xe6\x94\xaf\xe6\x8c\x81: 30s, 2m, 1h, 500ms, 1d \xe7\xad\x89\n",
    /* S_EXAMPLES    */ "\n\xe7\xa4\xba\xe4\xbe\x8b:\n",
    /* S_ERR_INTERVAL*/ "\xe9\x94\x99\xe8\xaf\xaf: \xe6\x97\xa0\xe6\x95\x88\xe7\x9a\x84\xe9\x97\xb4\xe9\x9a\x94\xe6\xa0\xbc\xe5\xbc\x8f\n",
    /* S_ERR_UNKNOWN */ "\xe6\x9c\xaa\xe7\x9f\xa5\xe5\x8f\x82\xe6\x95\xb0\n",
    /* S_ERR_NO_INPUT*/ "\xe9\x94\x99\xe8\xaf\xaf: \xe8\xaf\xb7\xe4\xbd\xbf\xe7\x94\xa8 -i \xe6\x8c\x87\xe5\xae\x9a\xe8\xbe\x93\xe5\x85\xa5\xe8\xa7\x86\xe9\xa2\x91\xe6\x96\x87\xe4\xbb\xb6\n",
    /* S_OUT_DIR     */ "\xe8\xbe\x93\xe5\x87\xba\xe7\x9b\xae\xe5\xbd\x95: %s\n",
    /* S_ERR_MKDIR   */ "\xe9\x94\x99\xe8\xaf\xaf: \xe6\x97\xa0\xe6\xb3\x95\xe5\x88\x9b\xe5\xbb\xba\xe7\x9b\xae\xe5\xbd\x95 '%s'\n",
    /* S_ERR_OPEN_VID*/ "\xe9\x94\x99\xe8\xaf\xaf: \xe6\x97\xa0\xe6\xb3\x95\xe6\x89\x93\xe5\xbc\x80\xe8\xa7\x86\xe9\xa2\x91 '%s'\n",
    /* S_ERR_STREAM  */ "\xe9\x94\x99\xe8\xaf\xaf: \xe6\x97\xa0\xe6\xb3\x95\xe8\x8e\xb7\xe5\x8f\x96\xe6\xb5\x81\xe4\xbf\xa1\xe6\x81\xaf\n",
    /* S_ERR_NO_VIDEO*/ "\xe9\x94\x99\xe8\xaf\xaf: \xe6\x9c\xaa\xe6\x89\xbe\xe5\x88\xb0\xe8\xa7\x86\xe9\xa2\x91\xe6\xb5\x81\n",
    /* S_ERR_CODEC   */ "\xe9\x94\x99\xe8\xaf\xaf: \xe4\xb8\x8d\xe6\x94\xaf\xe6\x8c\x81\xe7\x9a\x84\xe7\xbc\x96\xe8\xa7\xa3\xe7\xa0\x81\xe5\x99\xa8\n",
    /* S_ERR_DECODER */ "\xe9\x94\x99\xe8\xaf\xaf: \xe6\x97\xa0\xe6\xb3\x95\xe6\x89\x93\xe5\xbc\x80\xe8\xa7\xa3\xe7\xa0\x81\xe5\x99\xa8\n",
    /* S_VIDEO_INFO  */ "\xe8\xa7\x86\xe9\xa2\x91: %dx%d, \xe9\x97\xb4\xe9\x9a\x94: %.2f \xe7\xa7\x92\n",
    /* S_CAPTURING   */ "\xe5\xbc\x80\xe5\xa7\x8b\xe6\x88\xaa\xe5\x9b\xbe...\n",
    /* S_DONE        */ "\n\xe5\xae\x8c\xe6\x88\x90! \xe5\x85\xb1\xe6\x88\xaa\xe5\x8f\x96 %d \xe5\xb8\xa7 -> %s\n",
    /* S_ERR_INT_FMT */ "\xe9\x94\x99\xe8\xaf\xaf: \xe6\x97\xa0\xe6\x95\x88\xe7\x9a\x84\xe9\x97\xb4\xe9\x9a\x94\xe6\xa0\xbc\xe5\xbc\x8f '%s'\n",
    /* S_EX1         */ "  Kohane -i \xe8\xa7\x86\xe9\xa2\x91.mp4\n",
    /* S_EX2         */ "  Kohane -i \xe8\xa7\x86\xe9\xa2\x91.mp4 -s 1m\n",
    /* S_EX3         */ "  Kohane -i \xe8\xa7\x86\xe9\xa2\x91.mp4 -s 500ms\n",
    /* S_FRAME_FMT   */ "  [%04d] %s  (%.2f \xe7\xa7\x92)\n",
};

#define T(i) (g_lang ? S_zh[i] : S_en[i])

static void detect_language(void)
{
#ifdef _WIN32
    LANGID lid = GetUserDefaultUILanguage();
    if ((lid & 0xFF) == 0x04) g_lang = 1; /* Chinese */
#else
    const char *lang = getenv("LANG");
    if (!lang) lang = getenv("LC_ALL");
    if (lang && strstr(lang, "zh")) g_lang = 1;
#endif
}

/* ──────────────────── FFmpeg log callback ──────────────────── */

/* Suppress noisy FFmpeg decode errors after seek */
static void ffmpeg_log_cb(void *ptr, int level, const char *fmt, va_list vl)
{
    if (level > AV_LOG_ERROR) return;
    static const char *const filters[] = {
        "NAL", "missing picture", "co located", "POC",
        "non-existing PPS", "decode_slice_header error",
        "no frame!", "Invalid NAL", "error while decoding",
        NULL
    };
    char msg[1024];
    vsnprintf(msg, sizeof(msg), fmt, vl);
    for (int j = 0; filters[j]; j++)
        if (strstr(msg, filters[j])) return;
    fprintf(stderr, "%s\n", msg);
}

/* ──────────────────── Encoding helpers (Windows) ──────────────────── */

#ifdef _WIN32
/* System codepage -> UTF-8 (for FFmpeg paths) */
static char *sys_to_utf8(const char *src)
{
    int wlen = MultiByteToWideChar(CP_ACP, 0, src, -1, NULL, 0);
    if (wlen <= 0) return _strdup(src);
    wchar_t *wstr = (wchar_t *)malloc(sizeof(wchar_t) * wlen);
    MultiByteToWideChar(CP_ACP, 0, src, -1, wstr, wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    char *utf8 = (char *)malloc(ulen);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, utf8, ulen, NULL, NULL);
    free(wstr);
    return utf8;
}
/* UTF-8 -> System codepage (for mkdir/fopen) */
static void utf8_to_sys(const char *utf8, char *out, int out_sz)
{
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wlen <= 0) { strncpy(out, utf8, out_sz); out[out_sz-1] = 0; return; }
    wchar_t *wstr = (wchar_t *)malloc(sizeof(wchar_t) * wlen);
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wstr, wlen);
    WideCharToMultiByte(CP_ACP, 0, wstr, -1, out, out_sz, NULL, NULL);
    free(wstr);
}
/* wchar_t* -> UTF-8 (for wmain argument conversion) */
static char *wchar_to_utf8(const wchar_t *src)
{
    int ulen = WideCharToMultiByte(CP_UTF8, 0, src, -1, NULL, 0, NULL, NULL);
    if (ulen <= 0) return NULL;
    char *utf8 = (char *)malloc(ulen);
    WideCharToMultiByte(CP_UTF8, 0, src, -1, utf8, ulen, NULL, NULL);
    return utf8;
}
/* UTF-8 path fopen (Windows fopen does not support UTF-8) */
static FILE *fopen_utf8(const char *path, const char *mode)
{
    wchar_t wpath[1024], wmode[32];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024);
    MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 32);
    return _wfopen(wpath, wmode);
}
#endif

/* ──────────────────── Time parsing ──────────────────── */

/*
 * Parse time string, return microseconds.
 * English: 30s / 2m / 1h / 1d / 500ms / 1000us / sec / min / hour / day
 * Chinese: 30\xe7\xa7\x92 / 2\xe5\x88\x86 / 1\xe5\xb0\x8f\xe6\x97\xb6 / 1\xe5\xa4\xa9 /
 *          500\xe6\xaf\xab\xe7\xa7\x92 / 1000\xe5\xbe\xae\xe7\xa7\x92
 * Bare number defaults to seconds.
 */
static int64_t parse_time(const char *str)
{
    char   *end = NULL;
    double  val = strtod(str, &end);

    if (end == str) return -1;
    if (!end || *end == '\0') return (int64_t)(val * 1000000);

    /* English units */
    if      (!strcmp(end, "us"))                        return (int64_t)val;
    else if (!strcmp(end, "ms"))                        return (int64_t)(val * 1000);
    else if (!strcmp(end, "s") || !strcmp(end, "sec"))  return (int64_t)(val * 1000000);
    else if (!strcmp(end, "m") || !strcmp(end, "min"))  return (int64_t)(val * 60000000LL);
    else if (!strcmp(end, "h") || !strcmp(end, "hour")) return (int64_t)(val * 3600000000LL);
    else if (!strcmp(end, "d") || !strcmp(end, "day"))  return (int64_t)(val * 86400000000LL);

    /* Chinese units (UTF-8) */
    if      (!strcmp(end, "\xe7\xa7\x92"))              return (int64_t)(val * 1000000);        /* \xe7\xa7\x92 = sec */
    else if (!strcmp(end, "\xe5\x88\x86"))              return (int64_t)(val * 60000000LL);     /* \xe5\x88\x86 = min */
    else if (!strcmp(end, "\xe5\xb0\x8f\xe6\x97\xb6"))  return (int64_t)(val * 3600000000LL);   /* \xe5\xb0\x8f\xe6\x97\xb6 = hour */
    else if (!strcmp(end, "\xe5\xa4\xa9"))              return (int64_t)(val * 86400000000LL);  /* \xe5\xa4\xa9 = day */
    else if (!strcmp(end, "\xe6\xaf\xab\xe7\xa7\x92"))  return (int64_t)(val * 1000);           /* \xe6\xaf\xab\xe7\xa7\x92 = ms */
    else if (!strcmp(end, "\xe5\xbe\xae\xe7\xa7\x92"))  return (int64_t)val;                    /* \xe5\xbe\xae\xe7\xa7\x92 = us */

    return -1;
}

/* ──────────────────── PNG writer ──────────────────── */

static int write_png(const char *path,
                     const uint8_t *rgb_data, int width, int height)
{
    FILE     *fp  = NULL;
    png_structp png  = NULL;
    png_infop   info = NULL;
    png_bytep  *rows = NULL;
    int y, ret = -1;

#ifdef _WIN32
    fp = fopen_utf8(path, "wb");
#else
    fp = fopen(path, "wb");
#endif
    if (!fp) { perror("fopen"); return -1; }

    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) goto cleanup;

    info = png_create_info_struct(png);
    if (!info) goto cleanup;

    if (setjmp(png_jmpbuf(png))) goto cleanup;

    png_init_io(png, fp);
    png_set_IHDR(png, info,
                 (png_uint_32)width, (png_uint_32)height,
                 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    rows = (png_bytep *)malloc(sizeof(png_bytep) * (size_t)height);
    if (!rows) goto cleanup;

    for (y = 0; y < height; y++)
        rows[y] = (png_bytep)(rgb_data + y * width * 3);

    png_write_image(png, rows);
    png_write_end(png, NULL);
    ret = 0;

cleanup:
    free(rows);
    if (info) png_destroy_info_struct(png, &info);
    if (png)  png_destroy_write_struct(&png, NULL);
    if (fp)   fclose(fp);
    return ret;
}

/* ──────────────────── Usage ──────────────────── */

static void usage(void)
{
    printf("%s", T(S_TITLE));
    printf("%s", T(S_USAGE));
    printf("%s", T(S_OPTIONS));
    printf("%s", T(S_OPT_I));
    printf("%s", T(S_OPT_S));
    printf("%s", T(S_OPT_S2));
    printf("%s", T(S_EXAMPLES));
    printf("%s", T(S_EX1));
    printf("%s", T(S_EX2));
    printf("%s", T(S_EX3));
}

/* ──────────────────── Main ──────────────────── */

#ifdef _WIN32
int wmain(int argc, wchar_t *wargv[])
#else
int main(int argc, char *argv[])
#endif
{
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif

    detect_language();

    /* Suppress noisy FFmpeg decode errors after seek */
    av_log_set_callback(ffmpeg_log_cb);
    av_log_set_level(AV_LOG_WARNING);

    const char    *input_file    = NULL;
    int64_t        interval_us   = 30 * 1000000LL;   /* default 30 sec */
    char           output_dir[512];
    char           out_path[1024];

    AVFormatContext *fmt_ctx   = NULL;
    AVCodecContext  *codec_ctx = NULL;
    AVFrame         *frame     = NULL;
    AVFrame         *rgb_frame = NULL;
    AVPacket        *pkt       = NULL;
    struct SwsContext *sws_ctx = NULL;
    uint8_t         *rgb_buf   = NULL;

    int video_idx, ret, count, i;
    int64_t next_capture;
    const AVCodec *codec;
    int rgb_buf_size;

#ifdef _WIN32
    char *input_utf8 = NULL;
#endif

    /* ── Parse command line ── */
    for (i = 1; i < argc; i++) {
#ifdef _WIN32
        wchar_t *arg = wargv[i];
        if (!wcscmp(arg, L"-i") && i + 1 < argc) {
            input_utf8 = wchar_to_utf8(wargv[++i]);
            input_file = input_utf8;
        } else if (!wcscmp(arg, L"-s") && i + 1 < argc) {
            char tmp[256];
            WideCharToMultiByte(CP_UTF8, 0, wargv[++i], -1, tmp, sizeof(tmp), NULL, NULL);
            interval_us = parse_time(tmp);
            if (interval_us <= 0) {
                fprintf(stderr, "%s", T(S_ERR_INTERVAL));
                return 1;
            }
        } else if (!wcscmp(arg, L"-h") || !wcscmp(arg, L"--help")) {
#else
        if (!strcmp(argv[i], "-i") && i + 1 < argc) {
            input_file = argv[++i];
        } else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            interval_us = parse_time(argv[++i]);
            if (interval_us <= 0) {
                fprintf(stderr, T(S_ERR_INTERVAL_FMT), argv[i]);
                return 1;
            }
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
#endif
            usage();
            return 0;
        } else {
            fprintf(stderr, "%s", T(S_ERR_UNKNOWN));
            usage();
            return 1;
        }
    }

    if (!input_file) {
        fprintf(stderr, "%s", T(S_ERR_NO_INPUT));
        usage();
        return 1;
    }

    /* ── Create output directory ── */
    {
#ifdef _WIN32
        /* Extract basename from input_file (UTF-8) */
        const char *base = strrchr(input_file, '/');
        const char *bs   = strrchr(input_file, '\\');
        if (bs && bs > base) base = bs;
        base = base ? base + 1 : input_file;

        /* Output dir in system codepage (for mkdir) */
        char sys_dir[512];
        snprintf(output_dir, sizeof(output_dir), "%s_frames", base);
        utf8_to_sys(output_dir, sys_dir, sizeof(sys_dir));
        if (MKDIR(sys_dir) != 0 && errno != EEXIST) {
            fprintf(stderr, T(S_ERR_MKDIR), output_dir);
            free(input_utf8);
            return 1;
        }
#else
        const char *base = strrchr(input_file, '/');
        base = base ? base + 1 : input_file;
        snprintf(output_dir, sizeof(output_dir), "%s_frames", base);
        if (MKDIR(output_dir) != 0 && errno != EEXIST) {
            fprintf(stderr, T(S_ERR_MKDIR), output_dir);
            return 1;
        }
#endif
        printf(T(S_OUT_DIR), output_dir);
    }

    /* ── Open video ── */
    ret = avformat_open_input(&fmt_ctx, input_file, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, T(S_ERR_OPEN_VIDEO), input_file);
        return 1;
    }

    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "%s", T(S_ERR_STREAM_INFO));
        avformat_close_input(&fmt_ctx);
        return 1;
    }

    /* ── Find video stream ── */
    video_idx = -1;
    for (i = 0; i < (int)fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_idx = i;
            break;
        }
    }
    if (video_idx < 0) {
        fprintf(stderr, "%s", T(S_ERR_NO_VIDEO));
        avformat_close_input(&fmt_ctx);
        return 1;
    }

    /* ── Initialize decoder ── */
    codec = avcodec_find_decoder(fmt_ctx->streams[video_idx]->codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "%s", T(S_ERR_CODEC));
        avformat_close_input(&fmt_ctx);
        return 1;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, fmt_ctx->streams[video_idx]->codecpar);
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "%s", T(S_ERR_DECODER));
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return 1;
    }

    /* ── Prepare RGB conversion ── */
    frame     = av_frame_alloc();
    rgb_frame = av_frame_alloc();
    pkt       = av_packet_alloc();

    rgb_frame->format = AV_PIX_FMT_RGB24;
    rgb_frame->width  = codec_ctx->width;
    rgb_frame->height = codec_ctx->height;
    av_frame_get_buffer(rgb_frame, 0);

    sws_ctx = sws_getContext(
        codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
        codec_ctx->width, codec_ctx->height, AV_PIX_FMT_RGB24,
        SWS_FAST_BILINEAR, NULL, NULL, NULL);

    rgb_buf_size = av_image_get_buffer_size(
        AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height, 1);
    rgb_buf = (uint8_t *)av_malloc((size_t)rgb_buf_size);

    printf(T(S_VIDEO_INFO), codec_ctx->width, codec_ctx->height,
           (double)interval_us / 1000000.0);
    printf("%s", T(S_CAPTURING));

    /* ── Capture frames (smart seek + sequential decode hybrid) ── */
    count        = 0;
    next_capture = 0;
    {
        int     sequential = 0;      /* 1 = sequential decode mode */
        int64_t decode_pos = 0;      /* current decode position (us, estimated) */
        int64_t seek_threshold = interval_us * 3; /* seek vs sequential threshold */

        while (1) {
            int64_t duration = fmt_ctx->duration;
            if (duration > 0 && next_capture >= duration) break;

            /* Decide whether to seek or decode sequentially */
            int do_seek = 0;
            if (!sequential) {
                /* First time or target is far ahead -> seek */
                if (next_capture > decode_pos + seek_threshold)
                    do_seek = 1;
            }

            if (do_seek) {
                ret = av_seek_frame(fmt_ctx, -1, next_capture,
                                    AVSEEK_FLAG_BACKWARD);
                if (ret < 0) { sequential = 1; continue; }

                /* Drain decoder residual frames, then flush */
                avcodec_send_packet(codec_ctx, NULL);
                while (avcodec_receive_frame(codec_ctx, frame) == 0)
                    av_frame_unref(frame);
                avcodec_flush_buffers(codec_ctx);
                decode_pos = next_capture - interval_us; /* estimate seek landing point */
            }

            /* Decode frames until we reach the target timestamp */
            int found = 0;
            while (av_read_frame(fmt_ctx, pkt) >= 0) {
                if (pkt->stream_index != video_idx) {
                    av_packet_unref(pkt);
                    continue;
                }

                ret = avcodec_send_packet(codec_ctx, pkt);
                av_packet_unref(pkt);
                if (ret < 0) continue;  /* corrupted packet, skip */

                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    int64_t pts = frame->best_effort_timestamp;
                    if (pts == AV_NOPTS_VALUE) pts = frame->pts;
                    if (pts == AV_NOPTS_VALUE) { av_frame_unref(frame); continue; }

                    AVRational tb = fmt_ctx->streams[video_idx]->time_base;
                    int64_t cur_time = av_rescale_q(pts, tb, AV_TIME_BASE_Q);
                    decode_pos = cur_time;

                    if (cur_time < next_capture) {
                        /* Fell behind target by too much -> switch to sequential */
                        if (next_capture > 0 &&
                            cur_time < next_capture - seek_threshold) {
                            sequential = 1;
                        }
                        av_frame_unref(frame);
                        continue;
                    }

                    /* Target frame found, capture */
                    sws_scale(sws_ctx,
                              (const uint8_t * const *)frame->data,
                              frame->linesize, 0, codec_ctx->height,
                              rgb_frame->data, rgb_frame->linesize);
                    av_frame_unref(frame);

                    snprintf(out_path, sizeof(out_path),
                             "%s/frame_%04d.png", output_dir, count);

                    if (write_png(out_path,
                                  rgb_frame->data[0],
                                  codec_ctx->width,
                                  codec_ctx->height) == 0) {
                        printf(T(S_FRAME_FMT),
                               count, out_path,
                               (double)cur_time / 1000000.0);
                        count++;
                    }

                    next_capture += interval_us;
                    found = 1;
                    break;
                }
                if (found) break;
            }

            if (!found) break;  /* no more frames, done */
        }
    }

    printf(T(S_DONE), count, output_dir);

    /* ── Cleanup ── */
    av_free(rgb_buf);
    sws_freeContext(sws_ctx);
    av_packet_free(&pkt);
    av_frame_free(&rgb_frame);
    av_frame_free(&frame);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    return 0;
}

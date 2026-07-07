#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* 抑制 FFmpeg 解码错误刷屏 (seek 后部分损坏包属正常现象) */
static void ffmpeg_log_cb(void *ptr, int level, const char *fmt, va_list vl)
{
    /* 只输出 ERROR 级别, 且过滤已知的无害解码噪声 */
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

/* ──────────────────── 编码转换 (Windows) ──────────────────── */

#ifdef _WIN32
/* 系统代码页 -> UTF-8 (用于 FFmpeg 路径) */
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
/* UTF-8 -> 系统代码页 (用于 mkdir/fopen) */
static void utf8_to_sys(const char *utf8, char *out, int out_sz)
{
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wlen <= 0) { strncpy(out, utf8, out_sz); out[out_sz-1] = 0; return; }
    wchar_t *wstr = (wchar_t *)malloc(sizeof(wchar_t) * wlen);
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wstr, wlen);
    WideCharToMultiByte(CP_ACP, 0, wstr, -1, out, out_sz, NULL, NULL);
    free(wstr);
}
/* wchar_t* -> UTF-8 (用于 wmain 参数转换) */
static char *wchar_to_utf8(const wchar_t *src)
{
    int ulen = WideCharToMultiByte(CP_UTF8, 0, src, -1, NULL, 0, NULL, NULL);
    if (ulen <= 0) return NULL;
    char *utf8 = (char *)malloc(ulen);
    WideCharToMultiByte(CP_UTF8, 0, src, -1, utf8, ulen, NULL, NULL);
    return utf8;
}
/* UTF-8 路径 fopen (Windows fopen 不支持 UTF-8) */
static FILE *fopen_utf8(const char *path, const char *mode)
{
    wchar_t wpath[1024], wmode[32];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024);
    MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 32);
    return _wfopen(wpath, wmode);
}
#endif

/* ──────────────────── 时间解析 ──────────────────── */

/*
 * 解析标准 Unix 时间字符串, 返回微秒值.
 * 支持格式: 30s / 2m / 1h / 1d / 500ms / 1000us
 *            或长写: sec / min / hour / day
 *            纯数字默认按秒计
 */
static int64_t parse_time(const char *str)
{
    char   *end = NULL;
    double  val = strtod(str, &end);

    if (end == str) return -1;
    if (!end || *end == '\0') return (int64_t)(val * 1000000);

    if      (!strcmp(end, "us"))              return (int64_t)val;
    else if (!strcmp(end, "ms"))              return (int64_t)(val * 1000);
    else if (!strcmp(end, "s") || !strcmp(end, "sec"))  return (int64_t)(val * 1000000);
    else if (!strcmp(end, "m") || !strcmp(end, "min"))  return (int64_t)(val * 60000000LL);
    else if (!strcmp(end, "h") || !strcmp(end, "hour")) return (int64_t)(val * 3600000000LL);
    else if (!strcmp(end, "d") || !strcmp(end, "day"))  return (int64_t)(val * 86400000000LL);

    return -1;
}

/* ──────────────────── PNG 写入 ──────────────────── */

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

/* ──────────────────── 帮助信息 ──────────────────── */

static void usage(void)
{
    printf("Kohane - 视频热截图工具 (帧提取器)\n\n");
    printf("用法: Kohane -i <视频文件> [-s <间隔>]\n\n");
    printf("选项:\n");
    printf("  -i <文件>   输入视频文件路径\n");
    printf("  -s <间隔>   截图间隔 (默认: 30s)\n");
    printf("              支持: 30s, 2m, 1h, 500ms, 1d 等\n");
    printf("\n示例:\n");
    printf("  Kohane -i video.mp4\n");
    printf("  Kohane -i video.mp4 -s 1m\n");
    printf("  Kohane -i video.mp4 -s 500ms\n");
}

/* ──────────────────── 主函数 ──────────────────── */

#ifdef _WIN32
int wmain(int argc, wchar_t *wargv[])
#else
int main(int argc, char *argv[])
#endif
{
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif

    /* 抑制 FFmpeg 解码错误刷屏 */
    av_log_set_callback(ffmpeg_log_cb);
    av_log_set_level(AV_LOG_WARNING);

    const char    *input_file    = NULL;
    int64_t        interval_us   = 30 * 1000000LL;   /* 默认 30 秒 */
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

    /* ── 解析命令行 ── */
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
                fprintf(stderr, "错误: 无效的间隔格式\n");
                return 1;
            }
        } else if (!wcscmp(arg, L"-h") || !wcscmp(arg, L"--help")) {
#else
        if (!strcmp(argv[i], "-i") && i + 1 < argc) {
            input_file = argv[++i];
        } else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            interval_us = parse_time(argv[++i]);
            if (interval_us <= 0) {
                fprintf(stderr, "错误: 无效的间隔格式 '%s'\n", argv[i]);
                return 1;
            }
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
#endif
            usage();
            return 0;
        } else {
            fprintf(stderr, "未知参数\n");
            usage();
            return 1;
        }
    }

    if (!input_file) {
        fprintf(stderr, "错误: 请使用 -i 指定输入视频文件\n");
        usage();
        return 1;
    }

    /* ── 创建输出目录 ── */
    {
#ifdef _WIN32
        /* 从 input_file (UTF-8) 提取 basename */
        const char *base = strrchr(input_file, '/');
        const char *bs   = strrchr(input_file, '\\');
        if (bs && bs > base) base = bs;
        base = base ? base + 1 : input_file;

        /* 输出目录用系统代码页 (供 mkdir/fopen 使用) */
        char sys_dir[512];
        snprintf(output_dir, sizeof(output_dir), "%s_frames", base);
        utf8_to_sys(output_dir, sys_dir, sizeof(sys_dir));
        if (MKDIR(sys_dir) != 0 && errno != EEXIST) {
            fprintf(stderr, "错误: 无法创建目录 '%s'\n", output_dir);
            free(input_utf8);
            return 1;
        }
#else
        const char *base = strrchr(input_file, '/');
        base = base ? base + 1 : input_file;
        snprintf(output_dir, sizeof(output_dir), "%s_frames", base);
        if (MKDIR(output_dir) != 0 && errno != EEXIST) {
            fprintf(stderr, "错误: 无法创建目录 '%s'\n", output_dir);
            return 1;
        }
#endif
        printf("输出目录: %s\n", output_dir);
    }

    /* ── 打开视频 ── */
    ret = avformat_open_input(&fmt_ctx, input_file, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "错误: 无法打开视频 '%s'\n", input_file);
        return 1;
    }

    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "错误: 无法获取流信息\n");
        avformat_close_input(&fmt_ctx);
        return 1;
    }

    /* ── 查找视频流 ── */
    video_idx = -1;
    for (i = 0; i < (int)fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_idx = i;
            break;
        }
    }
    if (video_idx < 0) {
        fprintf(stderr, "错误: 未找到视频流\n");
        avformat_close_input(&fmt_ctx);
        return 1;
    }

    /* ── 初始化解码器 ── */
    codec = avcodec_find_decoder(fmt_ctx->streams[video_idx]->codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "错误: 不支持的编解码器\n");
        avformat_close_input(&fmt_ctx);
        return 1;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, fmt_ctx->streams[video_idx]->codecpar);
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "错误: 无法打开解码器\n");
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return 1;
    }

    /* ── 准备 RGB 转换 ── */
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

    printf("视频: %dx%d, 间隔: %.2f 秒\n",
           codec_ctx->width, codec_ctx->height,
           (double)interval_us / 1000000.0);
    printf("开始截图...\n");

    /* ── 按间隔截图 (智能 seek + 顺序解码混合) ── */
    count        = 0;
    next_capture = 0;
    {
        int     sequential = 0;      /* 1 = 顺序解码模式 */
        int64_t decode_pos = 0;      /* 当前解码位置 (微秒, 估算) */
        int64_t seek_threshold = interval_us * 3; /* seek vs 顺序的阈值 */

        while (1) {
            int64_t duration = fmt_ctx->duration;
            if (duration > 0 && next_capture >= duration) break;

            /* 决定 seek 还是顺序解码 */
            int do_seek = 0;
            if (!sequential) {
                /* 首次或目标在前方较远处 → seek */
                if (next_capture > decode_pos + seek_threshold)
                    do_seek = 1;
            }

            if (do_seek) {
                ret = av_seek_frame(fmt_ctx, -1, next_capture,
                                    AVSEEK_FLAG_BACKWARD);
                if (ret < 0) { sequential = 1; continue; }

                /* drain 解码器残留帧, 然后 flush */
                avcodec_send_packet(codec_ctx, NULL);
                while (avcodec_receive_frame(codec_ctx, frame) == 0)
                    av_frame_unref(frame);
                avcodec_flush_buffers(codec_ctx);
                decode_pos = next_capture - interval_us; /* 估计 seek 落点 */
            }

            /* 解码帧, 直到找到目标时间的帧 */
            int found = 0;
            while (av_read_frame(fmt_ctx, pkt) >= 0) {
                if (pkt->stream_index != video_idx) {
                    av_packet_unref(pkt);
                    continue;
                }

                ret = avcodec_send_packet(codec_ctx, pkt);
                av_packet_unref(pkt);
                if (ret < 0) continue;  /* 损坏的包, 跳过 */

                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    int64_t pts = frame->best_effort_timestamp;
                    if (pts == AV_NOPTS_VALUE) pts = frame->pts;
                    if (pts == AV_NOPTS_VALUE) { av_frame_unref(frame); continue; }

                    AVRational tb = fmt_ctx->streams[video_idx]->time_base;
                    int64_t cur_time = av_rescale_q(pts, tb, AV_TIME_BASE_Q);
                    decode_pos = cur_time;

                    if (cur_time < next_capture) {
                        /* 落后目标超过阈值 → 切顺序模式 */
                        if (next_capture > 0 &&
                            cur_time < next_capture - seek_threshold) {
                            sequential = 1;
                        }
                        av_frame_unref(frame);
                        continue;
                    }

                    /* 找到目标帧, 截图 */
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
                        printf("  [%04d] %s  (%.2fs)\n",
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

            if (!found) break;  /* 读不到帧, 结束 */
        }
    }

    printf("\n完成! 共截取 %d 帧 -> %s\n", count, output_dir);

    /* ── 清理 ── */
    av_free(rgb_buf);
    sws_freeContext(sws_ctx);
    av_packet_free(&pkt);
    av_frame_free(&rgb_frame);
    av_frame_free(&frame);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#define MKDIR(path) mkdir(path, 0755)
#endif

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <png.h>

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

    fp = fopen(path, "wb");
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

int main(int argc, char *argv[])
{
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
    int64_t timestamp, cur_time;
    int got_frame;
    const AVCodec *codec;
    int rgb_buf_size;

    /* ── 解析命令行 ── */
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc) {
            input_file = argv[++i];
        } else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            interval_us = parse_time(argv[++i]);
            if (interval_us <= 0) {
                fprintf(stderr, "错误: 无效的间隔格式 '%s'\n", argv[i]);
                return 1;
            }
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage();
            return 0;
        } else {
            fprintf(stderr, "未知参数: %s\n", argv[i]);
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
        const char *base = strrchr(input_file, '/');
        const char *bs   = strrchr(input_file, '\\');
        if (bs && bs > base) base = bs;
        base = base ? base + 1 : input_file;

        snprintf(output_dir, sizeof(output_dir), "%s_frames", base);
        if (MKDIR(output_dir) != 0 && errno != EEXIST) {
            fprintf(stderr, "错误: 无法创建目录 '%s'\n", output_dir);
            return 1;
        }
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
        SWS_BILINEAR, NULL, NULL, NULL);

    rgb_buf_size = av_image_get_buffer_size(
        AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height, 1);
    rgb_buf = (uint8_t *)av_malloc((size_t)rgb_buf_size);

    printf("视频: %dx%d, 间隔: %.2f 秒\n",
           codec_ctx->width, codec_ctx->height,
           (double)interval_us / 1000000.0);
    printf("开始截图...\n");

    /* ── 按间隔 seek 并截图 ── */
    count     = 0;
    timestamp = 0;

    while (1) {
        int64_t duration = fmt_ctx->duration;
        if (duration > 0 && timestamp >= duration) break;

        ret = av_seek_frame(fmt_ctx, video_idx, timestamp, AVSEEK_FLAG_BACKWARD);
        if (ret < 0) break;

        avcodec_flush_buffers(codec_ctx);

        got_frame = 0;
        cur_time  = -1;

        while (av_read_frame(fmt_ctx, pkt) >= 0) {
            if (pkt->stream_index == video_idx) {
                avcodec_send_packet(codec_ctx, pkt);

                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    int64_t ft = frame->pts;
                    if (ft == AV_NOPTS_VALUE)
                        ft = pkt->pts;

                    if (ft != AV_NOPTS_VALUE) {
                        AVRational tb = fmt_ctx->streams[video_idx]->time_base;
                        cur_time = av_rescale_q(ft, tb, AV_TIME_BASE_Q);
                    }

                    if (cur_time < 0 || cur_time >= timestamp) {
                        sws_scale(sws_ctx,
                                  (const uint8_t * const *)frame->data,
                                  frame->linesize, 0, codec_ctx->height,
                                  rgb_frame->data, rgb_frame->linesize);

                        snprintf(out_path, sizeof(out_path),
                                 "%s/frame_%04d.png", output_dir, count);

                        if (write_png(out_path,
                                      rgb_frame->data[0],
                                      codec_ctx->width,
                                      codec_ctx->height) == 0) {
                            printf("  [%04d] %s",
                                   count, out_path);
                            if (cur_time >= 0)
                                printf("  (%.2fs)",
                                       (double)cur_time / 1000000.0);
                            printf("\n");
                            count++;
                        }
                        got_frame = 1;
                        break;
                    }
                }
                av_packet_unref(pkt);
                if (got_frame) break;
            } else {
                av_packet_unref(pkt);
            }
        }

        if (!got_frame) {
            printf("  [跳过] %.2fs 处无可用帧\n",
                   (double)timestamp / 1000000.0);
        }

        timestamp += interval_us;
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

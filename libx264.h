#ifndef LIBX264_H
#define LIBX264_H

#include <x264.h>

AVCodec ngtc_x264_encoder; // Global Codec structure

typedef struct ngtc_X264Context {
    x264_param_t params;
    x264_t *enc;
    x264_picture_t pic;
    uint8_t *sei;
    int sei_size;
    AVFrame out_pic;
} ngtc_X264Context;

// NGTC Custom Codec Settings
typedef struct ngtc_X264Codec {
    // Psy
    float psy_rd;
    float psy_trellis;
    // AQ
    int aq;
    float aq_strength;
    // MB-Tree
    int mbtree;
    int lookahead;
    // Slices
    int h264_slices;
    // Stats
    int ssim;
} ngtc_X264Codec;

int ngtc_X264_frame(AVCodecContext *ctx, uint8_t *buf, int bufsize, void *data);
av_cold int ngtc_X264_close(AVCodecContext *avctx);
av_cold int ngtc_X264_init(AVCodecContext *avctx);

#endif

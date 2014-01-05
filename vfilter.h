#ifndef VFILTER_H
#define VFILTER_H

#include "lav.h"
#include "codec.h"

struct AVInputStream;

typedef struct AVOutputStream {
    AVStream *st;            /* stream in the output file */
    int video_crop;
    int topBand;
    int leftBand;
    int frame_topBand;
    int frame_bottomBand;
    int frame_leftBand;
    int frame_rightBand;
    int sws_opts;
} AVOutputStream;

typedef struct AVInputStream {
    AVStream *st;
    int64_t       pts;       /* current pts */
    AVFilterContext *out_video_filter;
    AVFilterContext *input_video_filter;
    AVFrame *filter_frame;
    int has_filter_frame;
    AVFilterBufferRef *picref;
} AVInputStream;

// publicly available functions
int configure_filters(AVFilterGraph *graph, char *vfilters, AVInputStream *ist, AVOutputStream *ost, struct CodecSettings *cs);
int get_filtered_video_frame(AVFilterContext *ctx, AVFrame *frame,
                             AVFilterBufferRef **picref_ptr, AVRational *tb);

/* FFMPEG API missing */
int av_vsrc_buffer_add_frame(AVFilterContext *buffer_filter, AVFrame *frame,
                              int64_t pts, AVRational pixel_aspect);

#endif

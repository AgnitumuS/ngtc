#include "vfilter.h"

typedef struct {
    enum PixelFormat pix_fmt;
} FFSinkContext;

static int ffsink_init(AVFilterContext *ctx, const char *args, void *opaque)
{
    FFSinkContext *priv = ctx->priv;

    if (!opaque)
        return AVERROR(EINVAL);
    *priv = *(FFSinkContext *)opaque;

    return 0;
}

static void null_end_frame(AVFilterLink *inlink) { }

static int ffsink_query_formats(AVFilterContext *ctx)
{
    FFSinkContext *priv = ctx->priv;
    enum PixelFormat pix_fmts[] = { priv->pix_fmt, PIX_FMT_NONE };

    avfilter_set_common_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

AVFilter ffsink = {
    .name      = "ffsink",
    .priv_size = sizeof(FFSinkContext),
    .init      = ffsink_init,

    .query_formats = ffsink_query_formats,

    .inputs    = (AVFilterPad[]) {{ .name          = "default",
                                    .type          = AVMEDIA_TYPE_VIDEO,
                                    .end_frame     = null_end_frame,
                                    .min_perms     = AV_PERM_READ, },
                                  { .name = NULL }},
    .outputs   = (AVFilterPad[]) {{ .name = NULL }},
};

int get_filtered_video_frame(AVFilterContext *ctx, AVFrame *frame,
                             AVFilterBufferRef **picref_ptr, AVRational *tb)
{
    int ret;
    AVFilterBufferRef *picref;

    if ((ret = avfilter_request_frame(ctx->inputs[0])) < 0)
        return ret;
    if (!(picref = ctx->inputs[0]->cur_buf))
        return AVERROR(ENOENT);
    *picref_ptr = picref;
    ctx->inputs[0]->cur_buf = NULL;
    *tb = ctx->inputs[0]->time_base;

    memcpy(frame->data,     picref->data,     sizeof(frame->data));
    memcpy(frame->linesize, picref->linesize, sizeof(frame->linesize));
    frame->interlaced_frame = picref->video->interlaced;
    frame->top_field_first  = picref->video->top_field_first;

    return 1;
}

int configure_filters(AVFilterGraph *graph, char *vfilters, AVInputStream *ist, AVOutputStream *ost, struct CodecSettings *cs)
{
    AVFilterContext *last_filter, *filter;
    /** filter graph containing all filters including input & output */
    AVCodecContext *codec = ost->st?ost->st->codec:NULL;
    AVCodecContext *icodec = ist->st->codec;
    FFSinkContext ffsink_ctx = { .pix_fmt = 0 };
    char args[255];
    int ret;

    if (codec)
    	ffsink_ctx.pix_fmt = codec->pix_fmt ;

    graph = av_mallocz(sizeof(AVFilterGraph));

    if ((ret = avfilter_open(&ist->input_video_filter, avfilter_get_by_name("buffer"), "src")) < 0)
        return ret;
    if (( ret = avfilter_open(&ist->out_video_filter, &ffsink, "out")) < 0)
        return ret;

    snprintf(args, 255, "%d:%d:%d:%d:%d", ist->st->codec->width,
             ist->st->codec->height, ist->st->codec->pix_fmt,
	     ist->st->time_base.num, ist->st->time_base.den);
    if ((ret = avfilter_init_filter(ist->input_video_filter, args, NULL)) < 0)
        return ret;
    if (( ret = avfilter_init_filter(ist->out_video_filter, NULL, &ffsink_ctx)) < 0)
        return ret;

    /* add input and output filters to the overall graph */
    avfilter_graph_add_filter(graph, ist->input_video_filter);
    avfilter_graph_add_filter(graph, ist->out_video_filter);

    last_filter = ist->input_video_filter;

    if (ost->video_crop) {
        snprintf(args, 255, "%d:%d:%d:%d", 
                 icodec->width-(ost->leftBand+ost->frame_rightBand),
                 icodec->height-(ost->topBand+ost->frame_bottomBand),
		ost->leftBand, ost->topBand
		 );
	if ((ret = avfilter_open(&filter, avfilter_get_by_name("crop"), NULL)) < 0)
		return ret;
        if ((ret = avfilter_init_filter(filter, args, NULL)) < 0)
            return ret;
        if ((ret = avfilter_link(last_filter, 0, filter, 0)) < 0)
            return ret;
        last_filter = filter;
        avfilter_graph_add_filter(graph, last_filter);
    }

    if((cs->w !=
        icodec->width - (ost->frame_leftBand + ost->frame_rightBand)) ||
       (cs->h != icodec->height - (ost->frame_topBand  + ost->frame_bottomBand))) {
        snprintf(args, 255, "%d:%d:flags=0x%X",
                 codec->width,
                 codec->height,
                 ost->sws_opts);

	if ((ret = avfilter_open(&filter, avfilter_get_by_name("scale"), NULL)) < 0)
	    return ret;
        if ((ret = avfilter_init_filter(filter, args, NULL)) < 0)
            return ret;
        if ((ret = avfilter_link(last_filter, 0, filter, 0)) < 0)
            return ret;
        last_filter = filter;
        avfilter_graph_add_filter(graph, last_filter);
    }

    snprintf(args, sizeof(args), "flags=0x%X", ost->sws_opts);
    graph->scale_sws_opts = av_strdup(args);

    if (vfilters) {
        AVFilterInOut *outputs = av_malloc(sizeof(AVFilterInOut));
        AVFilterInOut *inputs  = av_malloc(sizeof(AVFilterInOut));

        outputs->name    = av_strdup("in");
        outputs->filter_ctx  = last_filter;
        outputs->pad_idx = 0;
        outputs->next    = NULL;

        inputs->name    = av_strdup("out");
        inputs->filter_ctx  = ist->out_video_filter;
        inputs->pad_idx = 0;
        inputs->next    = NULL;

        if (avfilter_graph_parse(graph, vfilters, inputs, outputs, NULL) < 0)
            return -1;
        av_freep(&vfilters);
    } else {
        if (avfilter_link(last_filter, 0, ist->out_video_filter, 0) < 0)
            return -1;
    }

    /* configure all the filter links */
    if (avfilter_graph_config(graph, NULL))
    	return -1;

    if (codec) {
    	codec->width = ist->out_video_filter->inputs[0]->w;
    	codec->height = ist->out_video_filter->inputs[0]->h;
    }

    return 0;
}


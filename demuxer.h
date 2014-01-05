#ifndef DEMUXER_H
#define DEMUXER_H

#include "hqv.h"

typedef struct DemuxerSettings {
        AVInputFormat *fmt;
        AVFormatParameters *ap;
        AVFormatContext *pFormatCtx;
        AVCodecContext *pCodecCtxVideo;
        AVCodecContext *pCodecCtxAudio;
        AVCodecContext *pCodecCtxSub;
	char *input_format;
        int videoStream;
        int audioStream;
        int subStream;
        ByteIOContext *pb;
	uint8_t *stream_buffer;
        int streaming;
        int do_video;
        int do_audio;
	int do_sub;
} DemuxerSettings;

int setupDemuxerFormat(DemuxerSettings *ds, char *file, struct hqv_data *hqv);
int setupDemuxerCodecs(DemuxerSettings *ds);
void freeDemuxer(DemuxerSettings *ds);

#endif

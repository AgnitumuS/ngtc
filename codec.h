#ifndef CODEC_H
#define CODEC_H

#include "ngtc.h"

// Codec Settings Structure
typedef struct CodecSettings {
        char ts[512];
        char hostname[256];
        // Mux format
        AVOutputFormat *fmt;
        AVFormatContext *oc;
        // Streams
        AVStream *audio_st;
        AVStream *video_st;
        // Encoder Context
        AVCodecContext *aEncCtx;
        AVCodecContext *vEncCtx;
        // Codecs
        AVCodec *aEncCodec;
        AVCodec *vEncCodec;
	//
	int copy_audio;
	int copy_video;
	// Audio sample format
	int sample_fmt;
        // Codec ID
        char mux_format[25];
        char audio_codec[25];
        char video_codec[25];
        int audio_codec_id;
        int video_codec_id;
        // Misc Settings
        int mux;
        int do_demux;
        int do_video;
        int do_audio;
        int do_sub;
        int do_interlace;
        int do_threads;
        // Aspect Ratio
        AVRational dar;
        double ar;
	// Tags/Meta Information
        char title[512];
        char author[512];
        char copyright[512];
        char comment[512];
        char album[512];
        // Encoder Settings
	int crop_left;
	int crop_right;
	int crop_top;
	int crop_bottom;
	int ssim;
	int psnr;
        AVRational sar;
        AVRational ofps;
        double out_fps;
        int w;
        int h;
        int bitrate;
        int abitrate;
        int audio_quality;
        int achan;
        int arate;
        int64_t channel_layout;
        int audio_profile;
        // Extended Encoder Settings
        int sws_flags;
        int deinterlace;
        int hq;
        double bt;
        double qsquish;
        int crf;
        int cabac;
        int wpred;
	int weightp;
        int mixed_refs;
        char profile[30];
        double level;
        int fastpskip;
        int bpyramid;
        int aud;
        int partitions;
        int goplen;
        int gop;
        int refs;
        int maxrate;
        int minrate;
        int bufsize;
        int bframes;
        int bstrategy;
	// x264
        int mbtree;
        int lookahead;
        int aq;
        double aq_strength;
        double psy_rd;
        double psy_trellis;
	int slices;
	//
        int trellis;
        int nodeblock;
        int deblocka;
        int deblockb;
        int scthreshold;
        int subme;
        char me_method[25];
        int me_range;
	int chroma_me;
        int directpred;
        double qcomp;
        int nr;
        int muxrate;
        int packetsize;
        //
        struct hqv_data *hqv;
} CodecSettings;

void set_codec_defaults(struct CodecSettings *cs);
void init_codec(CodecSettings *cs);
int read_codec(CodecSettings *cs, char *filename);

#endif

#include <sched.h>
#if defined(SYS_MACOSX) || defined(SYS_FREEBSD)
#include <sys/sysctl.h>
#endif

#include "lav.h"
#include "hqv.h"
#include "codec.h"
#include "libx264.h"
#include "encoder.h"

// Number of CPU's on system
static int cpu_num(void) {
        int np = 1;
#if defined (SYS_LINUX)
        unsigned int bit;
        cpu_set_t p_aff;
        memset( &p_aff, 0, sizeof(p_aff) );
        sched_getaffinity( 0, sizeof(p_aff), &p_aff );
        for( np = 0, bit = 0; bit < sizeof(p_aff); bit++ )
                np += (((uint8_t *)&p_aff)[bit / 8] >> (bit % 8)) & 1;
        return np;
#elif defined(SYS_MACOSX) || defined(SYS_FREEBSD)
        size_t length = sizeof( np );
        if( sysctlbyname("hw.ncpu", &np, &length, NULL, 0) )
        {
                np = 1;
        }
        return np;
#else
        return np;
#endif
}

/**********************/
/* A/V Encoding Muxer */
/**********************/
int init_muxer(CodecSettings *cs, char *o_filename) {
        time_t t;
        struct tm tmbase;

        t = time(NULL);
        localtime_r(&t,&tmbase);

#if (LIBAVCODEC_VERSION_MINOR > 33 && LIBAVCODEC_VERSION_MAJOR >= 52)
        cs->fmt = av_guess_format(cs->mux_format, NULL, NULL);
#else
        cs->fmt = guess_format(cs->mux_format, NULL, NULL);
#endif
        if (!cs->fmt) {
                av_log(NULL, AV_LOG_FATAL, "\rCould not find suitable output format\n");
                return -1;
        }
        cs->oc = avformat_alloc_context();
        if (!cs->oc) {
                av_log(NULL, AV_LOG_FATAL, "\rCould not allocate output context for format\n");
                return -1;
        }
        cs->oc->oformat = cs->fmt;

        cs->oc->flags |= AVFMT_FLAG_NONBLOCK;
	cs->oc->flags |= AVFMT_FLAG_GENPTS;
#ifdef AVFMT_FLAG_IGNDTS
        cs->oc->flags |= AVFMT_FLAG_IGNDTS;
#endif

        current_date(cs->ts);

        /*if (strcmp(cs->mux_format, "asf")) {
                if (!strcmp(cs->title, "")) {
                        sprintf(cs->oc->title, "(C) The Groovy Organization (%s [%s-%s-%d])",
                                cs->ts, cs->hqv->channel, cs->hostname, cs->hqv->device);
                } else
                        sprintf(cs->oc->title, "%s", cs->title);

                if (!strcmp(cs->author, ""))
                        sprintf(cs->oc->author, "Chris Kennedy");
                else
                        sprintf(cs->oc->author, "%s", cs->author);

                if (!strcmp(cs->copyright, ""))
                        sprintf(cs->oc->copyright, "(C) The Groovy Organization %d", tmbase.tm_year+1900);
                else
                        sprintf(cs->oc->copyright, "%s", cs->copyright);

                if (!strcmp(cs->comment, ""))
                        sprintf(cs->oc->comment, "NGTC Version %s by Chris Kennedy", NGTC_VERSION);
                else
                        sprintf(cs->oc->comment, "%s", cs->comment);

                if (!strcmp(cs->album, ""))
                        sprintf(cs->oc->album, "The Groovy Organization");
                else
                        sprintf(cs->oc->album, "%s", cs->album);
        }*/
        cs->oc->timestamp = parse_date(cs->ts, 0) / 1000000;

        if (o_filename)
                snprintf(cs->oc->filename, sizeof(cs->oc->filename), "%s", o_filename);

        return 0;
}

// Start Muxer
int start_muxer(CodecSettings *cs, char *o_filename) {
        if (av_set_parameters(cs->oc, NULL) < 0) {
                av_log(NULL, AV_LOG_FATAL, "\rInvalid output format parameters\n");
                return -1;
        }

        if (o_filename && !(cs->fmt->flags & AVFMT_NOFILE)) {
                if (url_fopen(&cs->oc->pb, o_filename, URL_WRONLY) < 0) {
                        av_log(NULL, AV_LOG_FATAL, "\rCould not open '%s'\n", o_filename);
                        return -1;
                }
        }
        if (o_filename)
                av_write_header(cs->oc);

        return 0;
}

// Mux Audio/Video frames, 0=audio 1=video
void mux_frame(CodecSettings *cs, uint8_t *outbuf, int size, int type, int rescale, int interleave) {
        AVCodecContext *c;
        AVStream *st = type?cs->video_st:cs->audio_st;
        AVFormatContext *oc = cs->oc;
        AVPacket pkt;
        int ret;

        // Check if output file is setup
        if (!cs->oc || !cs->oc->pb)
                return;

        av_init_packet(&pkt);

        c = st->codec;

        if (!type && rescale) // Audio duration
                pkt.duration = av_rescale((int64_t)cs->aEncCtx->frame_size*st->time_base.den,
                        st->time_base.num, cs->aEncCtx->sample_rate);

        if (c->coded_frame && c->coded_frame->pts != AV_NOPTS_VALUE)
                pkt.pts= av_rescale_q(c->coded_frame->pts, c->time_base, st->time_base);
        else
                pkt.pts = AV_NOPTS_VALUE;

        pkt.dts = AV_NOPTS_VALUE;

        if (!type || (c->coded_frame && c->coded_frame->key_frame))
                pkt.flags |= AV_PKT_FLAG_KEY;
        pkt.stream_index= st->index;
        pkt.data= outbuf;
        pkt.size = size;

        av_log(NULL, AV_LOG_DEBUG, "\r%s Mux Duration %d Packet PTS: %"PRId64" Frame PTS: %"PRId64" Stream PTS: %"PRId64"\n",
                type?"Video":"Audio", pkt.duration, (pkt.pts==AV_NOPTS_VALUE)?0:pkt.pts,
                (c->coded_frame&&c->coded_frame->pts!=AV_NOPTS_VALUE)?c->coded_frame->pts:0,
                (st->pts.val==AV_NOPTS_VALUE)?0:st->pts.val);

        if (interleave)
                ret = av_interleaved_write_frame(oc, &pkt);
        else
                ret = av_write_frame(oc, &pkt);

        if (ret!= 0) {
                av_log(NULL, AV_LOG_FATAL, "\rMux Error while writing %s frame, exiting!\n", type?"video":"audio");
                exit(1);
        }
}

// Stop Muxer
void stop_muxer(CodecSettings *cs) {
        int i;

        // Write End of File
        if (cs->oc && cs->oc->pb) {
                av_write_trailer(cs->oc);

                // Close File
                if (!(cs->fmt->flags & AVFMT_NOFILE))
                        url_fclose(cs->oc->pb);
        }

        /* free the streams */
        for(i = 0; i < cs->oc->nb_streams; i++) {
                if (cs->oc->streams[i]->codec->opaque)
                        av_free(cs->oc->streams[i]->codec->opaque);
                cs->oc->streams[i]->codec->opaque = NULL;
		if ((cs->oc->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && !cs->copy_audio) ||
			(cs->oc->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && !cs->copy_video) ||
			(cs->oc->streams[i]->codec->codec_type != AVMEDIA_TYPE_VIDEO &&
			cs->oc->streams[i]->codec->codec_type != AVMEDIA_TYPE_AUDIO))
                		avcodec_close(cs->oc->streams[i]->codec);
                av_free(cs->oc->streams[i]->codec);
                av_free(cs->oc->streams[i]);
        }

        cs->fmt = NULL;

        // Free Muxer and Output Codecs
        av_free(cs->oc);
}

/********************************************
 * Video Encoder
 *******************************************/
int init_video_encoder(CodecSettings *cs) {
        int flags = 0;
        int flags2 = 0;
        int partitions = 0;
        float mux_preload= 0.5;
        float mux_max_delay= 0.7;

        cs->video_st = av_new_stream(cs->oc, 0);
        cs->vEncCtx = cs->video_st->codec;
        cs->vEncCtx->codec_id = cs->video_codec_id;
        cs->vEncCtx->codec_type = CODEC_TYPE_VIDEO;

        if(cs->oc->oformat->flags & AVFMT_GLOBALHEADER)
                cs->vEncCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;

        cs->vEncCodec = avcodec_find_encoder_by_name(cs->video_codec);
        if (cs->vEncCodec == NULL) {
                av_log(NULL, AV_LOG_FATAL, "\rError: Codec for video encoder not found.\n");
                return -1;      // Codec not found
        }

        if (cs->video_codec_id == CODEC_ID_MPEG1VIDEO)
                cs->vEncCtx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

        if (!cs->nodeblock)
                flags |= CODEC_FLAG_LOOP_FILTER;

        if (cs->video_codec_id == CODEC_ID_H264) {
                if (cs->level > 0)
                        cs->vEncCtx->level = (int)round(10*cs->level);
                if (!strcmp(cs->profile, "high"))
                        flags2 |= CODEC_FLAG2_8X8DCT;
                if (cs->aud)
                        flags2 |= CODEC_FLAG2_AUD;

                if (cs->partitions & X264_PART_I4X4)
                        partitions |= X264_PART_I4X4;
                if (cs->partitions & X264_PART_I8X8)
                        partitions |= X264_PART_I8X8;
                if (cs->partitions & X264_PART_P8X8)
                        partitions |= X264_PART_P8X8;
                if (cs->partitions & X264_PART_P4X4)
                        partitions |= X264_PART_P4X4;
                if (cs->partitions & X264_PART_B8X8)
                        partitions |= X264_PART_B8X8;

#if (LIBAVCODEC_VERSION_MINOR > 33 && LIBAVCODEC_VERSION_MAJOR >= 52)
		cs->vEncCtx->weighted_p_pred = cs->weightp; 
#endif
        }

        if (cs->do_threads == -1) {
                // Threads per CPU + 1
                if (cpu_num() > 1)
                        cs->vEncCtx->thread_count = cpu_num()+1;
                else
                        cs->vEncCtx->thread_count = 1;
        } else
                cs->vEncCtx->thread_count = cs->do_threads;

        if (cs->vEncCtx->thread_count > 1)
                avcodec_thread_init(cs->video_st->codec, cs->vEncCtx->thread_count);

        av_log(NULL, AV_LOG_VERBOSE, "\rVideo Using %d threads for %d cpu's.\n",
                                cs->vEncCtx->thread_count, cpu_num());

        if (cs->wpred)
                flags2 |= CODEC_FLAG2_WPRED;
        if (cs->bpyramid)
                flags2 |= CODEC_FLAG2_BPYRAMID;
        if (cs->mixed_refs)
                flags2 |= CODEC_FLAG2_MIXED_REFS;
        if (cs->fastpskip)
                flags2 |= CODEC_FLAG2_FASTPSKIP;

	if (cs->psnr)
		flags |= CODEC_FLAG_PSNR;

        if (cs->video_codec_id == CODEC_ID_MPEG2VIDEO || cs->video_codec_id == CODEC_ID_MPEG1VIDEO) {
                flags |= CODEC_FLAG_CLOSED_GOP;
                flags2 |= CODEC_FLAG2_STRICT_GOP|CODEC_FLAG2_LOCAL_HEADER;
        }

        if (cs->do_interlace) {
                flags |= CODEC_FLAG_INTERLACED_DCT;
                flags |= CODEC_FLAG_INTERLACED_ME;
        }

        cs->vEncCtx->flags |= flags;
        cs->vEncCtx->flags2 |= flags2;
        cs->vEncCtx->pix_fmt = PIX_FMT_YUV420P;
        cs->vEncCtx->width = cs->w;
        cs->vEncCtx->height = cs->h;
        cs->vEncCtx->sample_aspect_ratio = cs->sar;

        if (cs->vEncCodec->supported_framerates)
                cs->ofps = cs->vEncCodec->supported_framerates[av_find_nearest_q_idx(cs->ofps, cs->vEncCodec->supported_framerates)];
        cs->out_fps = av_q2d(cs->ofps);
        cs->vEncCtx->time_base = (AVRational){cs->ofps.den,cs->ofps.num};
        //
        av_log(NULL, AV_LOG_VERBOSE, "\rSet Framerate: %d/%d\n", cs->ofps.num, cs->ofps.den);

        cs->video_st->sample_aspect_ratio = cs->sar;
        cs->video_st->time_base = cs->vEncCtx->time_base;

        // Quantizer levels
        if (cs->video_codec_id == CODEC_ID_H264) {
                cs->vEncCtx->qmin = 10;
                cs->vEncCtx->qmax = 51;
                cs->vEncCtx->i_quant_factor = 0.712;
                cs->vEncCtx->b_quant_factor = 1.30;
                cs->vEncCtx->chromaoffset = 0;
                cs->vEncCtx->max_qdiff = 4;

                // Cabac Mode
                if (cs->cabac && strcmp(cs->profile, "baseline"))
                        cs->vEncCtx->coder_type = FF_CODER_TYPE_AC;
        } else {
                cs->vEncCtx->qmin = 2;
                cs->vEncCtx->qmax = 31;
        }

        // Set GOP Size
        if (cs->goplen > 0)
                cs->vEncCtx->gop_size = (int)round(((double)cs->ofps.num/(double)cs->ofps.den) * (double)cs->goplen);
        else
                cs->vEncCtx->gop_size = cs->gop;

        cs->vEncCtx->keyint_min = (int)round((double)cs->ofps.num/(double)cs->ofps.den);

        if (cs->vEncCtx->keyint_min < cs->out_fps)
                cs->vEncCtx->keyint_min = (int)round(cs->out_fps);
        if (cs->vEncCtx->keyint_min > cs->vEncCtx->gop_size)
                cs->vEncCtx->keyint_min = cs->vEncCtx->gop_size;

        av_log(NULL, AV_LOG_VERBOSE, "\rVideo GOP size %d with keyframe min %d.\n",
                                cs->vEncCtx->gop_size, cs->vEncCtx->keyint_min);

        cs->vEncCtx->refs = cs->refs;
        cs->vEncCtx->crf = cs->crf;
        cs->vEncCtx->bit_rate = cs->bitrate;
        if (cs->bt >= 1)
                cs->vEncCtx->bit_rate_tolerance = cs->bt * cs->bitrate;
        cs->vEncCtx->rc_max_rate = cs->maxrate;
        cs->vEncCtx->rc_min_rate = cs->minrate;
        cs->vEncCtx->rc_buffer_size = cs->bufsize;
        if (cs->video_codec_id == CODEC_ID_H264) {
                if (strcmp(cs->profile, "baseline"))
                        cs->vEncCtx->max_b_frames = cs->bframes;
                else
                        cs->vEncCtx->max_b_frames = 0;
        } else
                cs->vEncCtx->max_b_frames = cs->bframes;

        if (cs->vEncCtx->max_b_frames)
                cs->vEncCtx->b_frame_strategy = cs->bstrategy;

        if (cs->video_codec_id == CODEC_ID_H264)
                cs->vEncCtx->me_method = calc_me(cs->me_method);
        else
                cs->vEncCtx->me_method = calc_me("epzs");

        if (cs->video_codec_id == CODEC_ID_MPEG2VIDEO || cs->video_codec_id == CODEC_ID_MPEG1VIDEO)
                cs->vEncCtx->scenechange_threshold = 1000000000;
        else
                cs->vEncCtx->scenechange_threshold = cs->scthreshold;

        if (cs->chroma_me > 0)
                cs->vEncCtx->me_cmp |= FF_CMP_CHROMA;
        cs->vEncCtx->me_range = cs->me_range;
        cs->vEncCtx->trellis = cs->trellis;
        cs->vEncCtx->deblockalpha = cs->deblocka;
        cs->vEncCtx->deblockbeta = cs->deblockb;
        cs->vEncCtx->directpred = cs->directpred;
        cs->vEncCtx->qcompress = cs->qcomp;
        cs->vEncCtx->me_subpel_quality = cs->subme;

        cs->vEncCtx->partitions = partitions;
        cs->vEncCtx->rc_qsquish = cs->qsquish;
        cs->vEncCtx->noise_reduction = cs->nr;

        // Custom H.264 Settings
        if (cs->video_codec_id == CODEC_ID_H264 && !strcmp(cs->vEncCodec->name, "ngtcx264")) {
                if (!cs->vEncCtx->opaque)
                        cs->vEncCtx->opaque = av_mallocz(sizeof(ngtc_X264Codec));
                if (!cs->vEncCtx->opaque) {
                        av_log(NULL, AV_LOG_FATAL, "Error allocating H.264 opaque data\n");
                        return -1;
                } else {
                        ngtc_X264Codec *codec = cs->vEncCtx->opaque;

                        codec->mbtree = cs->mbtree;
                        codec->lookahead = cs->lookahead;
                        codec->psy_rd = cs->psy_rd;
                        codec->psy_trellis = cs->psy_trellis;
                        codec->aq = cs->aq;
                        codec->aq_strength = cs->aq_strength;
                        codec->h264_slices = cs->slices;
                        codec->ssim = cs->ssim;
                }
        }

        // High Quality Mode
        if (cs->hq) {
                flags |= CODEC_FLAG_MV0;
                if (cs->video_codec_id == CODEC_ID_H264)
                        flags |= CODEC_FLAG_4MV;
                if (cs->vEncCtx->trellis)
                        flags |= CODEC_FLAG_CBP_RD;

                cs->vEncCtx->ildct_cmp = 2;
                cs->vEncCtx->me_cmp = 2;
                cs->vEncCtx->me_pre_cmp = 2;
                cs->vEncCtx->me_sub_cmp = 2;
                cs->vEncCtx->dia_size = 2;
                cs->vEncCtx->last_predictor_count = 3;
                cs->vEncCtx->pre_dia_size = 2;
                cs->vEncCtx->lmin = 1;
                cs->vEncCtx->mb_decision = FF_MB_DECISION_RD;

                if (cs->video_codec_id != CODEC_ID_MPEG2VIDEO && cs->video_codec_id != CODEC_ID_MPEG1VIDEO)
                        cs->vEncCtx->intra_dc_precision = 10;
        }

        if (!cs->vEncCtx->rc_initial_buffer_occupancy)
            cs->vEncCtx->rc_initial_buffer_occupancy = cs->vEncCtx->rc_buffer_size*3/4;

        if (cs->muxrate > 0)
                cs->oc->mux_rate = cs->muxrate;
        if (cs->packetsize > 0)
                cs->oc->packet_size = cs->packetsize;

        if (cs->video_codec_id == CODEC_ID_MPEG1VIDEO)
                mux_preload = (36000+3*1200) / 90000.0;

        cs->oc->preload= (int)(mux_preload*AV_TIME_BASE);
        cs->oc->max_delay= (int)(mux_max_delay*AV_TIME_BASE);

	return 0;
}

int open_video_encoder(CodecSettings *cs) {
        if (!cs->copy_video && avcodec_open(cs->vEncCtx, cs->vEncCodec) < 0) {
                av_log(NULL, AV_LOG_FATAL, "Error: Codec for video encoder failed to open.\n");
                return -1;      // Codec didn't open
        }
        return 0;
}

/************************
 * Audio Encoder
 ***********************/
int init_audio_encoder(CodecSettings *cs) {
        cs->audio_st = av_new_stream(cs->oc, 1);
        cs->aEncCtx = cs->audio_st->codec;
        cs->aEncCtx->codec_id = cs->audio_codec_id;
        cs->aEncCtx->codec_type = CODEC_TYPE_AUDIO;
        if(cs->oc->oformat->flags & AVFMT_GLOBALHEADER)
                cs->aEncCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;

        cs->aEncCodec = avcodec_find_encoder_by_name(cs->audio_codec);
        if (cs->aEncCodec == NULL) {
                av_log(NULL, AV_LOG_FATAL, "Error: Codec for audio encoder not found.\n");
                return -1;      // Codec not found
        }

        if (cs->audio_quality) {
                cs->aEncCtx->flags |= CODEC_FLAG_QSCALE;
                cs->aEncCtx->global_quality = /*FF_QP2LAMBDA **/ cs->audio_quality;
        }
        cs->aEncCtx->bit_rate = cs->abitrate;

        cs->aEncCtx->sample_rate = cs->arate;
        cs->aEncCtx->channels = cs->achan;
        cs->aEncCtx->channel_layout = cs->channel_layout;
        cs->aEncCtx->profile = cs->audio_profile;

        cs->aEncCtx->time_base = (AVRational){1, cs->aEncCtx->sample_rate};

        cs->audio_st->time_base = cs->aEncCtx->time_base;

	// Audio sample format for encoding output
	cs->aEncCtx->sample_fmt = cs->sample_fmt;

	if(cs->aEncCodec && cs->aEncCodec->sample_fmts){
		const enum SampleFormat *p= cs->aEncCodec->sample_fmts;
		for(; *p!=-1; p++){
			if(*p == cs->sample_fmt)
				break;
		}
		if(*p == -1)
			cs->aEncCtx->sample_fmt = cs->aEncCodec->sample_fmts[0];
	}

        return 0;
}

int open_audio_encoder(CodecSettings *cs) {
        if (!cs->copy_audio && avcodec_open(cs->aEncCtx, cs->aEncCodec) < 0) {
                av_log(NULL, AV_LOG_FATAL, "Error: Codec for audio encoder failed to open.\n");
                return -1;      // Codec didn't open
        }

	return 0;
}

/*************************************************
 * FLUSH VIDEO AND AUDIO STREAM 
 ************************************************/
int flush_video_encoder(CodecSettings *cs, uint8_t *video_outbuf, int video_outbuf_size, FILE *vFile) {
        AVFormatContext *oc = cs->oc;
        AVStream *st = cs->video_st;
        AVCodecContext *vEncCtx = cs->vEncCtx;
        int out_size = 1;
        int x = 0;
        int bytes = 0;

	if (cs->copy_video)
		return 0;

        av_log(NULL, AV_LOG_VERBOSE, "\rAttempting to flush video encoder...\n");

        // get the delayed frames 
        while(out_size > 0) {
                out_size = avcodec_encode_video(vEncCtx, video_outbuf, video_outbuf_size, NULL);
                if (out_size > 0) {
                        bytes += out_size;

                        if (oc && st && oc->pb) {
                                mux_frame(cs, video_outbuf, out_size,
                                        1, 0/*rescale*/,
                                        (cs->do_audio && cs->do_video && !cs->do_demux)/*interleave*/);
                        } else if (vFile) {
                                if (fwrite(video_outbuf, 1, out_size, vFile) != out_size) {
                			av_log(NULL, AV_LOG_ERROR, "Error: fwrite failed to write output video file.\n");
				}
			}

                        if (x == 0)
                                av_log(NULL, AV_LOG_VERBOSE, "\n");
                        av_log(NULL, AV_LOG_VERBOSE,
                                "\rVideo Flush/Write frame %3d (size=%5d) total: %d\n", ++x, out_size, bytes);
                }
        }
        if (bytes == 0)
                av_log(NULL, AV_LOG_VERBOSE, "\rFlush video encoder failed with output %d\n", out_size);

        return bytes;
}

int flush_audio_encoder(CodecSettings *cs, AVFifoBuffer *audio_fifo, uint8_t *audio_outbuf, uint8_t * bit_buffer, int bit_buffer_size, FILE *aFile, int do_padding) {
        AVFormatContext *oc = cs->oc;
        AVStream *st = cs->audio_st;
        AVCodecContext *aEncCtx = cs->aEncCtx;
        int osize = av_get_bits_per_sample_fmt(aEncCtx->sample_fmt) >> 3;
        int bytes = 0;
        int x = 0;

	if (cs->copy_audio)
		return 0;

        av_log(NULL, AV_LOG_VERBOSE, "\rAttempting to flush audio encoder...\n");

        for(;;) {
                int fs_tmp = aEncCtx->frame_size;
                int out_size = 0;
                int fifo_bytes = av_fifo_size(audio_fifo);
                int rescale = 0;

                if (fifo_bytes)
                        av_log(NULL, AV_LOG_VERBOSE, "Audio flush buffer contains %d bytes\n", fifo_bytes);

                if (aEncCtx->frame_size <= 1)
                        return 0;

                if (!do_padding && !(aEncCtx->codec->capabilities & CODEC_CAP_SMALL_LAST_FRAME)) {
                        int frame_bytes = aEncCtx->frame_size * osize * aEncCtx->channels;

                        if (fifo_bytes > frame_bytes)
                                fifo_bytes -= (fifo_bytes % frame_bytes);
                        else if (fifo_bytes < frame_bytes)
                                fifo_bytes = 0;

                        if (fifo_bytes) {
                                av_log(NULL, AV_LOG_VERBOSE, "Audio flush buffer [%d] frame_bytes aligned to %d bytes of %d fifo bytes\n",
                                        frame_bytes, fifo_bytes, av_fifo_size(audio_fifo));
                        } else
                                av_log(NULL, AV_LOG_VERBOSE, "Audio flush buffer not able to flush [%d] frame_bytes fifo has %d bytes\n",
                                        frame_bytes, av_fifo_size(audio_fifo));
                }

                if (fifo_bytes > 0) {
                        av_fifo_generic_read(audio_fifo, (uint8_t *)audio_outbuf, fifo_bytes, NULL );
                        if(aEncCtx->codec->capabilities & CODEC_CAP_SMALL_LAST_FRAME)
                                aEncCtx->frame_size = fifo_bytes / (osize * aEncCtx->channels);
                        else { // PAD
                                int frame_bytes = aEncCtx->frame_size * osize * aEncCtx->channels;
                                if (bit_buffer_size < frame_bytes) {
                                        av_log(NULL, AV_LOG_FATAL, "Audio flush error, bit buffer size %d too small for frame bytes %d\n",
                                                bit_buffer_size, frame_bytes);
                                        aEncCtx->frame_size = fs_tmp;
                                        return 0;
                                }
                                memset((uint8_t*)audio_outbuf+fifo_bytes, 0, frame_bytes - fifo_bytes);
                                av_log(NULL, AV_LOG_VERBOSE, "Audio flush padding last %d bytes in fifo to frame bytes %d\n",
                                        fifo_bytes, frame_bytes);
                        }

                        out_size = avcodec_encode_audio(aEncCtx, bit_buffer, bit_buffer_size, (short*)audio_outbuf);
                        rescale = 1;
                }
                if (out_size <= 0) {
                        out_size = avcodec_encode_audio(aEncCtx, bit_buffer, bit_buffer_size, NULL);
                        if (out_size > 0)
                                av_log(NULL, AV_LOG_VERBOSE, "Audio flush last %d bytes from encoder\n", out_size);
                }
                if (out_size < 0) {
                        av_log(NULL, AV_LOG_ERROR, "Audio flush encoding failed %d\n", out_size);
                        aEncCtx->frame_size = fs_tmp;
                        break;
                }

                // Got Data
                if (out_size > 0) {
                        bytes += out_size;

                        if (x == 0)
                                av_log(NULL, AV_LOG_VERBOSE, "\n");
                        av_log(NULL, AV_LOG_VERBOSE, "\rAudio Flush/Write frame %3d (size=%5d) total: %d fifo_size: %d frame_size: %d\n",
                                ++x, out_size, bytes, fifo_bytes, fs_tmp);

                        if (oc && st && oc->pb) {
                                mux_frame(cs, bit_buffer, out_size,
                                        0, rescale,
                                        (cs->do_audio && cs->do_video && !cs->do_demux));
                        } else if (aFile) {
                                if (fwrite(bit_buffer, 1,
                                           out_size, aFile) != out_size) {
                			av_log(NULL, AV_LOG_ERROR, "Error: fwrite failed to write output audio file.\n");
				}
			}
                }
                aEncCtx->frame_size = fs_tmp;
                if (out_size <= 0)
                        break;
        }
        return bytes;
}


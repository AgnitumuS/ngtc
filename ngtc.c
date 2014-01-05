/*
 *
 *  NGTC - New Great Transcoder (C) 2009,2010 Chris Kennedy
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <pthread.h>
#include <errno.h>

#include "ngtc.h"
#include "lav.h"
#include "hqv.h"
#include "codec.h"
#include "mp4box.h"
#include "demuxer.h"
#include "libx264.h"
#include "encoder.h"
#include "log.h"
#include "vfilter.h"

int continueDecoding = 1;
int debug = 1;

char slog[30] = "";
char elog[30] = "\n";

// AV Time structure
struct avts {
	int64_t rstart;
	int64_t astart;
	int64_t vstart;
	int64_t alastdts;
	int64_t vlastdts;
	int64_t slastdts;
	int64_t video_dvd_pts;
	int64_t audio_dvd_pts;
	int64_t dvd_segment_voffset;
	int64_t dvd_segment_aoffset;
	int64_t video_start_pts;
	int audio_wrap;
	int video_wrap;
	int64_t audio_wrap_ts;
	int64_t video_wrap_ts;
	int64_t audio_wrap_start_time;
	int64_t video_wrap_start_time;
	int64_t ptsgen_vdiff;
	int64_t ptsgen_adiff;
};


/*****************************************************
 * MISC FUNCTIONS UTLITY STUFF
 ****************************************************/
static void sighdlr(int sig)
{
	av_log(NULL, AV_LOG_DEBUG, "\rReceived signal: %d\n",sig);
	signal(sig,sighdlr);
	
	switch(sig) {
		case SIGINT: 
		case SIGTERM: 
		case SIGQUIT:
			continueDecoding = 0; 
			break;
		case SIGPIPE: 
			av_log(NULL, AV_LOG_WARNING, "\rReceived sigpipe: %d\n",sig);
			break;
		case SIGALRM: 
			av_log(NULL, AV_LOG_WARNING, "\rReceived sigalarm: %d\n",sig);
			break;
	}
}

// Get basename and extension from a filename separated by a character
int split_string(char *filename, char *base, char *ext, char sep) {
	char *extension;
	int ofb_len = 0;
	int elen;
	
	extension = strrchr(filename, sep);
	if (!extension) {
		av_log(NULL, AV_LOG_FATAL, "Error with %s input, extension=NULL.\n", filename);
		return -1;
	}
	
	elen = strlen(extension)-1;
	
	ofb_len = strlen(filename)-(elen+1);
	
	if (elen <= 0) {
		av_log(NULL, AV_LOG_FATAL, "Error in split, elen=%d!\n", elen);
		return -1;
	}
	
	if (base)
		strncpy(base, filename, ofb_len);
	else {
		av_log(NULL, AV_LOG_FATAL, "Error getting memory for filename basename\n");
		return -1;
	}
	base[ofb_len] = '\0';
	
	if (ext)
		strncpy(ext, extension+1, elen);
	else {
		av_log(NULL, AV_LOG_FATAL, "Error getting memory for filename base extension\n");
		return -1;
	}
	
	ext[elen] = '\0';
	
	return 0;
}

// Generic Date String
char *current_date(char *date)
{
	time_t t;
	struct tm tmbase;

	t = time(0);
	if(localtime_r(&t,&tmbase))
	{
		// UTC 2008-12-05 22:15:25
		if (date) {
			memset(date,0,sizeof(date));
		 	sprintf(date, "%d-%02d-%02d %02d:%02d:%02d", 
		 		tmbase.tm_year+1900, tmbase.tm_mon+1, tmbase.tm_mday, 
				tmbase.tm_hour, tmbase.tm_min, tmbase.tm_sec);
		} else
		 	av_log(NULL, AV_LOG_WARNING, "%d-%02d-%02d %02d:%02d:%02d", 
		 		tmbase.tm_year+1900, tmbase.tm_mon+1, tmbase.tm_mday, 
				tmbase.tm_hour, tmbase.tm_min, tmbase.tm_sec);
	}
	return date;
}

// Calculate SAR and DAR
static void calc_sar(CodecSettings *cs, int in_width, int in_height, AVRational in_sar) {
	double frame_aspect_ratio;
	AVRational out_dar;
	
	if(in_sar.num)
		frame_aspect_ratio = av_q2d(in_sar);
	else
		frame_aspect_ratio=1;
	
	// Get original DAR
	cs->dar = av_d2q(frame_aspect_ratio*in_width/in_height, 255);
	
	// Get original decimal AR
	frame_aspect_ratio *= (float) in_width / in_height;
	
	cs->ar = frame_aspect_ratio;
	
	// New Height
	if (cs->h <= 0) {
		// Auto Set height if width is set
		if (cs->w > 0) {
			double iw = in_width, ih = in_height, nw = cs->w;
			
			ih = cs->dar.den;
			iw = cs->dar.num;
			cs->h = nw * (ih/iw);		
			
			av_log(NULL, AV_LOG_VERBOSE, 
				"\rVideo Frame size [%dx%d] auto-calculated\n", cs->w, cs->h);
			
			// multiple of 16
			//if (cs->h%16 != 0)
			//	cs->h = (cs->h+(16-(cs->h%16)));
		} else {
			cs->h = in_height;
		}
		// multiple of 2
		if (cs->h%2 != 0)
			cs->h = (cs->h+(2-(cs->h%2)));
	}
	// New Width
	if (cs->w <= 0) {
		cs->w = in_width;
		
		// multiple of 2
		if (cs->w%2 != 0)
			cs->w = (cs->w+(2-(cs->w%2)));
	}
	
	// Calculate SAR and DAR
	
	// Get new SAR
	cs->sar = av_d2q(frame_aspect_ratio*cs->h/cs->w, 255);
	
	frame_aspect_ratio = av_q2d(cs->sar);
	
	// Get new DAR
	out_dar = av_d2q(frame_aspect_ratio*cs->w/cs->h, 255);
	
	av_log(NULL, AV_LOG_VERBOSE, 
			"\rVideo Source AR: %0.3f\n\rVideo Source SAR: %d:%d DAR: %d:%d\n"
			"\rVideo Output SAR: %d:%d Output DAR: %d:%d\n", 
			cs->ar, in_sar.num, in_sar.den, cs->dar.num, cs->dar.den, 
			cs->sar.num, cs->sar.den, out_dar.num, out_dar.den);
	return;
}

/**************************************************************
 * audio decoder preprocessing 
 **************************************************************/
static int resample_audio_frame(struct ReSampleContext *av_convert_ctx, int ichan, int ifmt, short *audio_sample, int in_size, 
								int ofmt, int ochan, short *audio_sample_out)
{
	int isize = av_get_bits_per_sample_fmt(ifmt) / 8;
	int osize = av_get_bits_per_sample_fmt(ofmt) / 8;
	int out_size;

	out_size = audio_resample(av_convert_ctx,
		  (short *)audio_sample_out,
		  (short *)audio_sample,
		  (in_size / (ichan * isize)));
	
	return out_size * ochan * osize;
}

/**************************************************************
 * video decoder preprocessing                                
 **************************************************************/
static void pre_process_video_frame(AVCodecContext *dec, AVPicture * pic, void **bufp)
{
	AVPicture *picture;
	AVPicture picture_tmp;
	AVPicture *picture_new;
	uint8_t *buf = 0;
	
	picture_new = pic;
	
	/* deinterlace : must be done before any resize */
	int size;
	
	/* create temporary picture */
	size = avpicture_get_size(dec->pix_fmt, dec->width, dec->height);
	buf = av_malloc(size);
	if (!buf)
		return;
	
	picture = &picture_tmp;
	avpicture_fill(picture, buf, dec->pix_fmt, dec->width, dec->height);
	
	if(avpicture_deinterlace(picture, picture_new,
			 dec->pix_fmt, dec->width, dec->height) < 0) {
		/* if error, do not deinterlace */
		av_log(NULL, AV_LOG_ERROR, "\rDeinterlacing failed\n");
		av_free(buf);
		buf = NULL;
		picture = picture_new;
	}
	
	if (picture_new != picture) {
		*picture_new = *picture;
	}
	*bufp = buf;
}

static int write_yuv_frame(FILE *vFile, AVFrame *picture, int height) {
	int bytes = 0;

	// Write out Raw Frame
	bytes += fwrite(picture->data[0], 1,
	   	(picture->linesize[0] * height),
		vFile);
	bytes += fwrite(picture->data[1], 1,
		(picture->linesize[1] * height) / 2,
		vFile);
	bytes += fwrite(picture->data[2], 1,
		(picture->linesize[2] * height) / 2,
		vFile);

	if (bytes != (picture->linesize[0] * height)
		+ ((picture->linesize[1] * height) / 2)
		+ ((picture->linesize[2] * height) / 2)) 
	{
		av_log(NULL, AV_LOG_ERROR, "Error writing YUV Frame, got %d back\n", bytes);
		return -1;
	}

	return bytes;
}

/**************************************************
 * DVD Muxer
 **************************************************/
static AVFormatContext *init_dvdmux(CodecSettings *cs, AVFormatContext *pFormatCtx, int videoStream, int audioStream, char *filename) {
	AVStream *i_vst, *i_ast;
	AVStream *vstream, *astream;
	AVCodecContext *venc, *aenc;
	AVCodecContext *i_venc, *i_aenc;
	AVFormatContext *oc;
	time_t t;
	struct tm tmbase;

	t = time(NULL);
	localtime_r(&t,&tmbase);

	i_vst = pFormatCtx->streams[videoStream];
	i_ast = pFormatCtx->streams[audioStream];

	i_venc = i_vst->codec;
	i_aenc = i_ast->codec;
	
	// Setup format muxer
	oc = avformat_alloc_context();
	if (!oc) {
		av_log(NULL, AV_LOG_FATAL, "\rCould not allocate dvd format context\n");
		return NULL;
	}
#if (LIBAVCODEC_VERSION_MINOR <= 33 && LIBAVCODEC_VERSION_MAJOR <= 52)
	oc->oformat = guess_format("dvd", NULL, NULL);
#else
	oc->oformat = av_guess_format("dvd", NULL, NULL);
#endif
	if (!oc->oformat) {
		av_log(NULL, AV_LOG_FATAL, "\rCould not allocate dvd format\n");
		return NULL;
	}

	oc->flags |= AVFMT_FLAG_NONBLOCK;
	oc->flags |=AVFMT_FLAG_GENPTS;
#ifdef AVFMT_FLAG_IGNDTS
	oc->flags |=AVFMT_FLAG_IGNDTS;
#endif

	if (filename)
		snprintf(oc->filename, sizeof(filename), "%s", filename);

	// Setup codecs
	vstream = av_new_stream(oc, videoStream);
	astream = av_new_stream(oc, audioStream);
	if (!vstream || !astream) {
		av_log(NULL, AV_LOG_FATAL, "\rCould not allocate dvd streams\n");
		return NULL;
	}
	
	venc = vstream->codec;
	aenc = astream->codec;

	venc->flags2 |= CODEC_FLAG2_LOCAL_HEADER;

	if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
		aenc->flags |= CODEC_FLAG_GLOBAL_HEADER;
		venc->flags |= CODEC_FLAG_GLOBAL_HEADER;
	}

	vstream->disposition = i_vst->disposition;
	venc->bits_per_raw_sample = i_venc->bits_per_raw_sample;
	venc->chroma_sample_location = i_venc->chroma_sample_location;

	vstream->stream_copy = 1;
	venc->codec_type = CODEC_TYPE_VIDEO;

	astream->stream_copy = 1;
	aenc->codec_type = CODEC_TYPE_AUDIO;

	venc->codec_id = i_venc->codec_id;
	venc->codec_tag = i_venc->codec_tag;
	venc->extradata = i_venc->extradata;
	venc->extradata_size = i_venc->extradata_size;

	aenc->codec_id = i_aenc->codec_id;
	aenc->codec_tag = i_aenc->codec_tag;
	aenc->extradata = i_aenc->extradata;
	aenc->extradata_size = i_aenc->extradata_size;

	vstream->sample_aspect_ratio = i_vst->sample_aspect_ratio;
	venc->time_base = i_venc->time_base;
	venc->height = i_venc->height;
	venc->width = i_venc->width;
	venc->sample_aspect_ratio = vstream->sample_aspect_ratio;
	venc->bit_rate = i_venc->bit_rate;
	venc->rc_max_rate = i_venc->rc_max_rate;
	venc->rc_min_rate = i_venc->rc_min_rate;
	venc->rc_buffer_size = 1835008;
	venc->pix_fmt = i_venc->pix_fmt;
	venc->has_b_frames = i_venc->has_b_frames;

	aenc->channels = i_aenc->channels;
	aenc->sample_rate = i_aenc->sample_rate;
	aenc->time_base = (AVRational){1, aenc->sample_rate};
	aenc->bit_rate = i_aenc->bit_rate;
	aenc->channel_layout = i_aenc->channel_layout;
	aenc->frame_size = i_aenc->frame_size;
	aenc->block_align = i_aenc->block_align;
	if (aenc->codec_id == CODEC_ID_AC3)
		aenc->block_align = 0;

	oc->packet_size = 2048;
	oc->mux_rate = 10080000;
	oc->preload= (int)(0.5*AV_TIME_BASE);
	oc->max_delay= (int)(0.7*AV_TIME_BASE);

	/*if (encode_streams) {
		AVCodec *aEncCodec, *vEncCodec;
		aEncCodec = avcodec_find_encoder(i_aenc->codec_id);
		if (!aEncCodec) {
			av_log(NULL, AV_LOG_FATAL, "\rCould not find dvd audio encoder\n");
			return NULL;
		}
		vEncCodec = avcodec_find_encoder(i_venc->codec_id);
		if (!vEncCodec) {
			av_log(NULL, AV_LOG_FATAL, "\rCould not find dvd video encoder\n");
			return NULL;
		}
		if (avcodec_open(venc, vEncCodec) < 0) {
			av_log(NULL, AV_LOG_FATAL, "\rCould not open dvd video encoder\n");
			return NULL;
		}
		if (avcodec_open(aenc, aEncCodec) < 0) {
			av_log(NULL, AV_LOG_FATAL, "\rCould not open dvd audio encoder\n");
			return NULL;
		}
	}*/

	// Meta Data
	current_date(cs->ts);

	oc->timestamp = parse_date(cs->ts, 0) / 1000000;
	
	// Setup encoding
	if (av_set_parameters(oc, NULL) < 0) {
		av_log(NULL, AV_LOG_FATAL, "\rCould not set parameters for dvd muxer\n");
		return NULL;
	}

	if (filename && !(oc->oformat->flags & AVFMT_NOFILE)) {
		if (url_fopen(&oc->pb, filename, URL_WRONLY) < 0) {
			av_log(NULL, AV_LOG_FATAL, "\rCould not open '%s'\n", filename);
			return NULL;
		}
	}

	if (filename)
		av_write_header(oc);

	return oc;
}

static int write_dvdmux(AVFormatContext *oc, AVPacket *pkt, int type, int64_t pts_offset, int rescale) {
	int ret;
	AVPacket newpkt;
	AVFrame avframe;

	avcodec_get_frame_defaults(&avframe);
	oc->streams[pkt->stream_index]->codec->coded_frame = &avframe;
	avframe.key_frame = pkt->flags & PKT_FLAG_KEY;

	av_init_packet(&newpkt);
	newpkt.stream_index = pkt->stream_index;
	newpkt.data = pkt->data;
	newpkt.size = pkt->size;

	if (rescale) {
		if (pkt->pts  != AV_NOPTS_VALUE) {
			newpkt.pts = av_rescale_q(pkt->pts-pts_offset, 
				oc->streams[pkt->stream_index]->time_base, 
				oc->streams[pkt->stream_index]->time_base);
		} else
			newpkt.pts = AV_NOPTS_VALUE;
		
		if (pkt->dts == AV_NOPTS_VALUE) {
			newpkt.dts = av_rescale_q(pkt->pts-pts_offset, 
				AV_TIME_BASE_Q, 
				oc->streams[pkt->stream_index]->time_base);
		} else
			newpkt.dts = av_rescale_q(pkt->dts-pts_offset, 
				oc->streams[pkt->stream_index]->time_base,
				oc->streams[pkt->stream_index]->time_base);

		newpkt.duration = av_rescale_q(pkt->duration, 
			oc->streams[pkt->stream_index]->time_base,
			oc->streams[pkt->stream_index]->time_base);
	} else {
		newpkt.duration = pkt->duration;
		newpkt.pts = pkt->pts-pts_offset;
		newpkt.dts = pkt->dts-pts_offset;
	}

	newpkt.flags = pkt->flags;
	
	av_log(NULL, AV_LOG_DEBUG, "\rMuxing DVD %s packet of size: %d pts: %"PRId64" dts: %"PRId64"\n", 
		type?"Video":"Audio", newpkt.size, newpkt.pts, newpkt.dts);	

	ret = av_interleaved_write_frame(oc, &newpkt);
	if (ret < 0)
		av_log(NULL, AV_LOG_FATAL, 
			"\rError (%d) muxing DVD %s Packet stream_index='%d' pts='%"PRId64"' dts='%"PRId64"' duration='%d' pts_offset '%"PRId64"'\n", 
			ret, type?"Video":"Audio", pkt->stream_index, pkt->pts, pkt->dts, pkt->duration, pts_offset);	

	oc->streams[pkt->stream_index]->codec->frame_number++;
	av_free_packet(&newpkt);
	return ret;
}

static int stop_dvdmux(AVFormatContext *oc) {
	int i;

	// Write End of File
	if (oc && oc->pb) {
		av_write_trailer(oc);
							
		// Close File
		if (!(oc->oformat->flags & AVFMT_NOFILE))
			url_fclose(oc->pb);
	}
							
	/* free the streams */
	for(i = 0; i < oc->nb_streams; i++) {
		av_free(oc->streams[i]->codec);
		av_free(oc->streams[i]);
	}

	// Free Muxer and Output Codecs
	av_free(oc);

	return 0;
}

/**************************************************************
 * MAIN PROGRAM BODY 
 ***************************************************************/
int main(int argc, char **argv)
{
	char *s;
	int i;
	// Input/Output Files
	int max_input_files = 512;
	int num_input_files = 0;
	int input_file_pos = 0;
	char *input_files[max_input_files];
	int num_codec_files = 0;
	char *codec_files[max_input_files];
	char *i_filename = NULL, *o_filename = NULL, *rawyuv = NULL, *rawpcm = NULL, *rawsub = NULL;
	char VideoFile[512] = "";
	char AudioFile[512] = "";
	char SubFile[512] = "";
	char OutputFile[512] = "";
	// DVD MPEG2 Muxing
	AVFormatContext *pMPEGAudioFormatCtx = NULL;
	AVFormatContext *pMPEGVideoFormatCtx = NULL;
	int64_t video_dvd_keyframe = 0;
	int dvd_audio_segment_needed = 0, dvd_video_segment_needed = 0;
	int is_keyframe = 0;
	int do_record_dvd = 1;
	int remux_timestamps = 0;
	int normalize_timestamps = 0;
	int rewrite_timestamps = 0;
	// Demuxer
	int videoStream, audioStream, subStream;
	AVFormatContext *pFormatCtx;
	AVCodecContext *pCodecCtxVideo = NULL;
	AVCodecContext *pCodecCtxAudio = NULL;
	AVCodecContext *pCodecCtxSub = NULL;
	AVPacket packet;
	AVSubtitle subtitle, *subtitle_to_free = NULL;
	int got_subtitle = 0;
	int frameFinished;
	// Video Decoding/Encoding
	int64_t audio_pts = 0, video_pts = 0;
	struct ReSampleContext *av_convert_ctx = NULL;
	struct SwsContext *img_convert_ctx = NULL;
	AVFrame pFrame, pFrameCrop;
	AVFrame picture;
	uint8_t *video_outbuf = NULL;
	int video_outbuf_size = 0;
	AVCodec *x264_enc = &ngtc_x264_encoder; // H.264 Encoder w/x264
	// Audio Decoding/Encoding
	uint8_t *audio_buf_out = NULL;
	uint8_t *audio_buf_out_rs = NULL;
	uint8_t *audio_outbuf = NULL;
	int audio_buf_size = 0;
	int audio_frame_size = 0;
	int audio_outbuf_size = 0;
	AVFifoBuffer *audio_fifo = NULL; 
	// Output file handles
	FILE *aFile = NULL, *vFile = NULL, *sFile = NULL;
	// Video Input
	double in_fps = 0;
	int in_width = 0;
	int in_height = 0;
	int in_vfmt = -1;
	// Audio Input
	int in_achan = 0;
	int in_arate = 0;
	int in_afmt = -1;
	int out_afmt = -1;
	// Sync variables
	int do_sync = 1;
	double drift = 0, drift_offset = 0;
	int frame_dup = 0, frame_drop = 0;
	// Stream Timing
	int64_t starttime = 0;
	int64_t tbn_v = 0, tbn_a = 0;
	int64_t video_bytes = 0, audio_bytes = 0;
	// Frame Count
	int64_t o_vcount = 0, vcount = 0, acount = 0, scount = 0;
	// Audio/Video durations
	double segmenttime = 0;
	double v_dur_in = 0, v_dur_out = 0;
	// Time Information
	double e_vtime = 0;
	double e_atime = 0;
	double vtime = -1;
	double atime = -1;
	// Audio/Video Time Structure
	struct avts AVTimestamp;
	// Deinterlacing
	void *buf_to_free = NULL;
	// Seconds to decode and start time
	int ss = 0, start_time = 0;
	// Segment time, close/reopen output file
	int video_segment_count = 1, audio_segment_count = 1;
	// File Segment Name Rotation
	char aout_filename_base[255] = "";
	char vout_filename_base[255] = "";
	char mout_filename_base[255] = "";
	char aext[8] = "";
	char vext[8] = "";
	char mext[8] = "";
	// Codec Structure
	int encode = 0;
	int mux_in_thread = 0;
	int csv_count = 0, csa_count = 0;
	CodecSettings *cs, *csa, *csv;
	CodecSettings streams[2];
	DemuxerSettings ds;
	// Threads
	int muxHandle = 0;
	int hqvHandle = 0;
	pthread_t muxThread;
	pthread_t hqvThread;
	struct mux_data *muxData = NULL;
	struct hqv_data *hqvData = NULL;
	// Raw A/V Input
	AVFormatParameters params, *ap = &params;
	AVInputFormat *infmt = NULL;
	char *input_format = NULL;
	char *input_video_codec = NULL;
	char *input_audio_codec = NULL;
	char *input_sub_codec = NULL;
	int input_sample_rate = 0;
	int input_channels = 0;
	int input_pix_fmt = PIX_FMT_YUV420P;
	int input_time_base_num = 0; // 29.97
	int input_time_base_den = 0; // 1
	int input_width = 0;
	int input_height = 0;
	//
	int no_audio = 0;
	int no_video = 0;
	int no_sub = 0;
	//
	int do_raw = 1;
	int do_record = 1;
	int do_overwrite = 0;
	int nice_level = 0;
	//
	int do_flush = 0;
	int last_frame = 0;
	// Help output
	int help = 0;
	int show_version = 0;
	int longhelp = 0;
	//
	int show_status = 0;
	//
	int do_resync = 0;
	int do_ptsgen = 0;
	//
	int do_vfilter = 1;
	static char *vfilters = NULL;
	AVFilterGraph *graph = NULL;
	AVOutputStream *ost;
	AVInputStream *ist;

	// Input/Output streams for filters
	ist = av_mallocz(sizeof(AVInputStream));
	ost = av_mallocz(sizeof(AVOutputStream));

	// Demuxer Settings structure
	memset(&ds, 0, sizeof(struct DemuxerSettings));

	// Codec Settings structure
	memset(&streams[0], 0, sizeof(struct CodecSettings));
	memset(&streams[1], 0, sizeof(struct CodecSettings));

	// Pointers for main/audio/video codec settings 
	cs = &streams[0];
	csa = &streams[0];
	csv = &streams[0];

	// Initialize codec structure to -1
	set_codec_defaults(cs);

	// HQV/DQV Capture thread structure
	hqvData = (struct hqv_data *) malloc(sizeof(struct hqv_data));
	memset(hqvData, 0, sizeof(struct hqv_data));

	// Initialize hqv structure
	set_hqv_defaults(hqvData);

	// Pointer to hqv structure in codec settings
	cs->hqv = hqvData;

	// AV Timestamp
	AVTimestamp.rstart = -1;
	AVTimestamp.astart = -1;
	AVTimestamp.vstart = -1;
	AVTimestamp.vlastdts = 0;
	AVTimestamp.alastdts = 0;
	AVTimestamp.slastdts = 0;
	AVTimestamp.video_dvd_pts = -1;
	AVTimestamp.audio_dvd_pts = -1;
	AVTimestamp.dvd_segment_voffset = 0;
	AVTimestamp.dvd_segment_aoffset = 0;
	AVTimestamp.video_start_pts = 0;
	AVTimestamp.audio_wrap = 0;
	AVTimestamp.video_wrap = 0;
	AVTimestamp.audio_wrap_ts = 0;
	AVTimestamp.video_wrap_ts = 0;
	AVTimestamp.audio_wrap_start_time = 0;
	AVTimestamp.video_wrap_start_time = 0;
	AVTimestamp.ptsgen_vdiff = 0;
	AVTimestamp.ptsgen_adiff = 0;
		
	// Input Command Line Args
	for (i = 1; i < argc; i++)
		if (*(s = argv[i]) == '-')
			switch (*(++s)) {
				case '-': // Long Args
					++s;
					if (!strcmp(s, "help")) {
						longhelp = 1;
						help = 1;
					} else if (!strcmp(s, "version")) {
						show_version= 1;
					} else if (!strcmp(s, "nosync")) {
						do_sync = 0;
					} else if (!strcmp(s, "resync")) {
						do_resync = 1;
					} else if (!strcmp(s, "ptsgen")) {
						do_ptsgen = 1;
					} else if (!strcmp(s, "ssim")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->ssim = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "vf")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							do_vfilter = 1;
							vfilters = av_malloc(2048);
							sprintf(vfilters, "%s", s);
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "psnr")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->psnr = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "title")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							sprintf(cs->title, "%s", s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "author")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							sprintf(cs->author, "%s", s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "copyright")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							sprintf(cs->copyright, "%s", s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "comment")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							sprintf(cs->comment, "%s", s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "album")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							sprintf(cs->album, "%s", s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "threads")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->do_threads = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "crop_top")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->crop_top = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "crop_bottom")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->crop_bottom = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "crop_left")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->crop_left = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "crop_right")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->crop_right = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "bframes")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->bframes = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "crf")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->crf = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "ofps")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->out_fps = atof(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "width")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->w = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "height")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->h = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "bitrate")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->bitrate = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "audio_quality")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->audio_quality = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "abitrate")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->abitrate = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "channels")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->achan = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "rate")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->arate = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "sws")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->sws_flags = calc_sws(strtol(s, NULL, 10));
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "interlace")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->do_interlace = atoi(s);	
							if (cs->do_interlace)
								cs->deinterlace = 0;	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "deinterlace")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->deinterlace = atoi(s);	
							if (cs->deinterlace)
								cs->do_interlace = 0;	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "hq")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->hq = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "bt")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->bt = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "qsquish")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->qsquish = atof(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "cabac")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->cabac = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "wpred")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->wpred = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "weightp")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->weightp = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "mixed_refs")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->mixed_refs = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "level")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->level = atof(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "profile")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							sprintf(cs->profile, "%s", s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "fastpskip")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->fastpskip = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "bpyramid")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->bpyramid = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "aud")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->aud = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "partitions")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->partitions = calc_partitions(s);
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "goplen")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->goplen = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "gop")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->gop = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "refs")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->refs = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "maxrate")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->maxrate = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "minrate")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->minrate = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "bufsize")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->bufsize = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "bstrategy")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->bstrategy = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "mbtree")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->mbtree = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "lookahead")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->lookahead = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "psy_rd")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->psy_rd = atof(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "psy_trellis")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->psy_trellis = atof(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "aq")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->aq = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "aq_strength")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->aq_strength = atof(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "slices")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->slices = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "trellis")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->trellis = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "nodeblock")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->nodeblock = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "deblocka")) {
						if ((i+1) < argc /*&& *(s = argv[i+1]) != '-'*/) {
							s = argv[i+1];
							cs->deblocka = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "deblockb")) {
						if ((i+1) < argc /*&& *(s = argv[i+1]) != '-'*/) {
							s = argv[i+1];
							cs->deblockb = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "subme")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->subme = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "chroma_me")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->chroma_me = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "scthreshold")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->scthreshold = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "me_method")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							sprintf(cs->me_method, "%s", s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "directpred")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->directpred = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "qcomp")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->qcomp = atof(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "nr")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->nr = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "muxrate")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->muxrate = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "packetsize")) {
						if ((i+1) < argc && *(s = argv[i+1]) != '-') {
							cs->packetsize = atoi(s);	
							i++; 
						} else
							help = 1;
					} else if (!strcmp(s, "formats")) {
						avcodec_register(x264_enc);
						av_register_all();
						avfilter_register_all();
						show_formats();
						exit(0);
					} else {
						fprintf(stderr, "Error with long arg %s\n", s);
						exit(1);
					}
					break;
				case 'h':
					help = 1;
					break;
				case 'I':
					// Input File Parameters
					switch(*(++s)) {
						case 'f': // Input Format
							if (argv[i][3]) 
								input_format = ++s;
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								input_format = s;	
								i++; 
							} else
								help = 1;
							break;
						case 'a': // Input Audio Codec
							if (argv[i][3]) 
								input_audio_codec = ++s;
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								input_audio_codec = s;	
								i++; 
							} else
								help = 1;
							break;
						case 'v': // Input Video Codec
							if (argv[i][3]) 
								input_video_codec = ++s;
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								input_video_codec = s;	
								i++; 
							} else
								help = 1;
							break;
						case 's': // Input Subtitle Codec
							if (argv[i][3]) 
								input_sub_codec = ++s;
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								input_sub_codec = s;	
								i++; 
							} else
								help = 1;
							break;
						case 'r': // Input Audio Sample Rate
							if (argv[i][3]) 
								input_sample_rate = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								input_sample_rate = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						case 'c': // Input Audio Channels
							if (argv[i][3]) 
								input_channels = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								input_channels = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						case 'w': // Input Video Width
							if (argv[i][3]) 
								input_width = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								input_width = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						case 'h': // Input Video Height
							if (argv[i][3]) 
								input_height = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								input_height = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						case 'p': // Input Video Pixel Format
							if (argv[i][3]) 
								input_pix_fmt = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								input_pix_fmt = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						case 'n': // Input FPS Num
							if (argv[i][3]) 
								input_time_base_num = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								input_time_base_num = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						case 'd': // Input FPS Den
							if (argv[i][3]) 
								input_time_base_den = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								input_time_base_den = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						default:
							fprintf(stderr, "Error with -I input %s, [favrcwhpnd] possible options\n", s);
							exit(1);
					}
					break;
				case 'O':
					// Output Parameters
					switch(*(++s)) {
						case 'l': // Deinterlace
							cs->deinterlace = 1;
							cs->do_interlace = 0;
							break;
						case 'f': // FPS
							if (argv[i][3]) 
								cs->out_fps = atof(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								cs->out_fps = atof(s);	
								i++; 
							} else
								help = 1;
							break;
						case 'w': // Width
							if (argv[i][3]) 
								cs->w = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								cs->w = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						case 'h': // Height
							if (argv[i][3]) 
								cs->h = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								cs->h = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						case 'r': // Audio Sample Rate
							if (argv[i][3]) 
								cs->arate = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								cs->arate = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						case 'c': // Audio Channels
							if (argv[i][3]) 
								cs->achan = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								cs->achan = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						default:
							fprintf(stderr, "Error with -O input %s, [emwhrcfl] possible options\n", s);
							exit(1);
							break;
					}
					break;
				case 'E':
					encode = 1;
					// Encoding Parameters
					switch(*(++s)) {
						case 'h': // High Quality
							if (argv[i][3]) 
								cs->hq = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								cs->hq = atoi(s);	
								i++; 
							} else
								cs->hq = 1;
							break;
						case 'f': // Mux Format
							if (argv[i][3]) 
								sprintf(cs->mux_format, "%s", ++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								sprintf(cs->mux_format, "%s", s);
								i++; 
							} else
								help = 1;
							break;
						case 'v': // Video Codec
							if (argv[i][3]) 
								sprintf(cs->video_codec, "%s", ++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								sprintf(cs->video_codec, "%s", s);
								i++; 
							} else
								help = 1;
							break;
						case 'a': // Audio Codec
							if (argv[i][3]) 
								sprintf(cs->audio_codec, "%s", ++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								sprintf(cs->audio_codec, "%s", s);
								i++; 
							} else
								help = 1;
							break;
						case 'b': // Bitrate
							switch(*(++s)) {
								case 'a': // Audio Bitrate
									if (argv[i][4]) 
										cs->abitrate = atoi(++s);
									else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
										cs->abitrate = atoi(s);	
										i++; 
									} else
										help = 1;
									break;
								case 'v': // Video Bitrate
									if (argv[i][4]) 
										cs->bitrate = atoi(++s);
									else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
										cs->bitrate = atoi(s);	
										i++; 
									} else
										help = 1;
									break;
								default:
									fprintf(stderr, "Error with -Eb input, [va] possible options\n");
									help = 1;
									break;
							}
							break;
						default:
							break;
					}
					break;
				case 'o': // Mux Output file
					if (!mux_in_thread)
						cs->mux = 1;
					encode = 1;

					if (argv[i][2]) 
						o_filename = ++s;
					else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
						o_filename = s;	
						i++; 
					} else
						help = 1;
					break;
				case 'i':
					if (argv[i][2]) {
						if (num_input_files == 0)
							i_filename = ++s;
						else 
							++s;
						if (num_input_files < max_input_files) {
							input_files[num_input_files] = s;
							num_input_files++;
						} else
							av_log(NULL, AV_LOG_WARNING, "Too many input files, only allow %d\n", max_input_files);
					} else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
						if (num_input_files == 0)
							i_filename = s;	
						i++; 

						if (num_input_files < max_input_files) {
							input_files[num_input_files] = s;
							num_input_files++;
						} else
							av_log(NULL, AV_LOG_WARNING, "Too many input files, only allow %d\n", max_input_files);
					} else
						help = 1;
					break;
				case 'v':
					if (argv[i][2] == 'n') {
						// No Video
						no_video = 1;
						break;
					} else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
						rawyuv = s;	
						i++; 
					}
					cs->do_video = 1;
					break;
				case 'a':
					if (argv[i][2] == 'n') {
						// No Audio
						no_audio = 1;
						break;
					} else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
						rawpcm = s;	
						i++; 
					}
					cs->do_audio = 1;
					break;
				case 's':
					if (argv[i][2] == 'n') {
						// No Subtitles
						no_sub = 1;
						break;
					} else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
						rawsub = s;	
						i++; 
					}
					cs->do_audio = 1;
					break;
				case 'd':
					if (argv[i][2]) {
						if (argv[i][2] == '-' && s++) 
							debug = -atoi(++s);
						else
							debug = atoi(++s);
					} else if ((i+1) < argc && *(s = argv[i+1])) {
						if (argv[i+1][0] == '-') 
							debug = -atoi(++s);
						else
							debug = atoi(s);
						if (debug < -2 || debug > 10) {
							fprintf(stderr, "Error: debug out of range (-1 to 10)\n");
							help = 1;
						} else
							i++;
					}
					break;
				case 'n':
					if (argv[i][2]) {
						if (argv[i][2] == '-' && s++) 
							nice_level = -atoi(++s);
						else
							nice_level = atoi(++s);
					} else if ((i+1) < argc && *(s = argv[i+1])) {
						if (argv[i+1][0] == '-') 
							nice_level = -atoi(++s);
						else
							nice_level = atoi(s);
						if (nice_level < -20 || nice_level > 20) {
							fprintf(stderr, "Error: nice level out of range (-20 to 20)\n");
							help = 1;
						} else
							i++;
					}
					break;
				case 'b': // Beginning time
					if (argv[i][2]) 
						start_time = atoi(++s);
					else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
						start_time = atoi(s);	
						i++; 
					} else
						help = 1;
					break;
				case 't': // Ending time
					if (argv[i][2]) 
						ss = atoi(++s);
					else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
						ss = atoi(s);	
						i++; 
					} else
						help = 1;
					break;
				case 'l':
					if (argv[i][2]) 
						hqvData->seconds = atoi(++s);
					else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
						hqvData->seconds = atoi(s);	
						i++; 
					} else
						help = 1;
					break;
				case 'Q':
					// HQV/DQV Common Parameters
					switch(*(++s)) {
						case 'n': // Device Number
							if (argv[i][3]) 
								hqvData->device = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								hqvData->device = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						case 'c': // Channel Name
							if (argv[i][3]) 
								hqvData->channel = ++s;
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								hqvData->channel = s;	
								i++; 
							} else
								help = 1;
							break;
						case 'r': // Round seconds to 00 on filename
							if (argv[i][3]) 
								hqvData->round = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								hqvData->round = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						case 'b': // Buffer Size
							if (argv[i][3]) 
								hqvData->read_size = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								hqvData->read_size = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						case 's': // Splice mode
							if (argv[i][3]) 
								hqvData->splice = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								hqvData->splice = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						case 'e': // Extended Mode
							if (argv[i][3]) 
								hqvData->em = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								hqvData->em = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						case 'a': // Align Time
							if (argv[i][3]) 
								hqvData->align = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								hqvData->align = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						default:
							fprintf(stderr, "Error with -Q input %s, [cnreasb] possible options\n", s);
							exit(1);
							break;
					}
					break;
				case 'H':
					// HQV Parameters
					hqvData->do_hqv = 1;
					switch(*(++s)) {
						case 'o': // Time offset in seconds
							if (argv[i][3]) 
								hqvData->offset = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								hqvData->offset = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						case 'K': // keep HQV in /data/tmp/ for tcmanageWMV.pl
							hqvData->keep = 1;
							break;
						case 'A': // Archive HQV in /data/hqv/
							if (argv[i][3]) 
								hqvData->archive = atoi(++s);
							else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
								hqvData->archive = atoi(s);	
								i++; 
							} else
								help = 1;
							break;
						case 'R': // Remux MPEG2 into DVD with libavformat
							hqvData->do_dvd = 1;
							break;
						case 'N': // Remux MPEG2 into DVD with libavformat and normalize timestamps
							hqvData->do_dvd = 1;
							normalize_timestamps = 1;
							break;
						default:
							break;
					}
					if (!hqvData->seconds)
						hqvData->seconds = 120;
					break;
				case 'D':
					// DQV Parameters
					hqvData->do_dqv = 1;

					if (!hqvData->seconds)
						hqvData->seconds = 120;
					break;
				case 'R':
					// Remux MPEG2 timestamps
					remux_timestamps = 1;
					do_ptsgen = 1;
					switch(*(++s)) {
						case 't': // rewrite timestamps
							rewrite_timestamps = 1;
							break;
						default:
							break;
					}
					break;
				case 'C':
					// Closed Captioning capture
					if (argv[i][2]) 
						hqvData->cc = atoi(++s);
					else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
						hqvData->cc = atoi(s);	
						i++; 
					} else {
						fprintf(stderr, "Error with -C option %s\n", s);
						exit(1);
					}
					break;
				case 'S': // Station ID
					if (argv[i][2]) 
						hqvData->stationid = atoi(++s);
					else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
						hqvData->stationid = atoi(s);	
						i++; 
					} else {
						fprintf(stderr, "Error with -S option %s\n", s);
						exit(1);
					}
					break;
				case 'e': // Extended Mode Range
					if (argv[i][2]) 
						sprintf(hqvData->em_range, "%s", ++s);
					else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
						sprintf(hqvData->em_range, "%s", s);
						i++; 
					} else {
						fprintf(stderr, "Error with -e option %s\n", s);
						exit(1);
					}
					break;
				case 'c':
					if (argv[i][2]) {
						++s;
						if (num_codec_files < max_input_files) {
							codec_files[num_codec_files] = s;
							num_codec_files++;
						} else
							av_log(NULL, AV_LOG_WARNING, "Too many codec files, only allow %d\n", max_input_files);
					} else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
						i++; 

						if (num_codec_files < max_input_files) {
							codec_files[num_codec_files] = s;
							num_codec_files++;
						} else
							av_log(NULL, AV_LOG_WARNING, "Too many codec files, only allow %d\n", max_input_files);
					} else
						help = 1;
					break;
				case 'm': // Mux with MP4Box in thread
					mux_in_thread = 1;
					encode = 1;
					if (argv[i][2]) 
						o_filename = ++s;
					else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
						o_filename = s;	
						i++; 
					}
					cs->mux = 0;
					break;
				case 'r': // Raw mode for demuxing
					if (argv[i][2]) 
						do_raw = atoi(++s);
					else if ((i+1) < argc && *(s = argv[i+1]) != '-') {
						do_raw = atoi(s);	
						i++; 
					} else {
						fprintf(stderr, "Error with -r option %s\n", s);
						exit(1);
					}
					if (!do_raw) 
						encode = 1;
					break;
				case 'y': // overwrite output
					do_overwrite = 1;
					break;
				default:
					fprintf(stderr, "Error with input %s\n", s);
					exit(1);
			}

	if (help || show_version) {
		printf("\nNGTC (New Great TransCoder) by Chris Kennedy (C) 2010");
		if (show_version) {
			printf("\n\nVersion: %s\n\n", NGTC_NUMBER_VERSION);
			printf("Build Date: %s\n\n", BUILD_DATE);
			printf("Compiler: GCC %s\n\n", __VERSION__);
			printf("Build System:\n  %s\n\n", SYSTEM_VERSION);
			printf("X264 Version 0.%d\n\n", X264_BUILD);
			printf("FFMPEG Libraries:\n");
			print_all_lib_versions(stderr, 1);
			fprintf(stderr, "\n");
			exit(0);
		} else
			printf(" (%s)\n\n", NGTC_VERSION);
		printf
		("Usage: %s -i <infile> [-c /codecs/dqv] [-v [<rawyuv_outfile>]] [-a [<rawpcm_outfile>]] [-o <muxed_outfile>]\n\n"
		 "  -i <file>      Input source file, can be used multiple times to combine files.\n"
		 "  -o <file>      Output file, compressed and muxed A/V.\n"
		 "  -a [file]      Audio inclusion, optional filename for demuxed output.\n"
		 "  -v [file]      Video inclusion, optional filename for demuxed output.\n"
		 "  -s [file]      Subtitle inclusion, optional filename for demuxed output.\n"
		 "  -c <file>      Codec file, can use this multiple times to combine codec files.\n"
		 "\nInput options (Raw A/V):\n"
		 "  -If <fmt>      Input file format.\n"
		 "  -Ia <codec>    Input file audio codec.\n"
		 "  -Ic <channels> Input file audio channels.\n"
		 "  -Ir <rate>     Input file audio rate.\n"
		 "  -Iv <codec>    Input file video codec.\n"
		 "  -Iw <width>    Input file video width.\n"
		 "  -Ih <height>   Input file video height.\n"
		 "  -Id <den>      Input file video fps denominator.\n"
		 "  -In <num>      Input file video fps numerator.\n"
		 "  -Ip <pixfmt>   Input file video pixel format.\n"
		 "  -Is <codec>    Input file subtitle codec.\n"
		 "\nOutput options:\n"
		 "  -Ol            Deinterlace video.\n"
		 "  -Ow <width>    Scale video to width.\n"
		 "  -Oh <height>   Scale video to height.\n"
		 "  -Of <fps>      New fps for video.\n"
		 "  -Oc <channels> Audio channels.\n"
		 "  -Or <arate>    Audio sample rate.\n"
		 "\nEncoding options:\n"
		 "  -E             Enable Encoding.\n"
		 "  -Ef  <fmt>     Mux format.\n"
		 "  -Ev  <codec>   Video codec.\n"
		 "  -Ea  <codec>   Audio codec.\n"
		 "  -Ebv <bitrate> Video bitrate.\n"
		 "  -Eba <bitrate> Audio bitrate.\n"
		 "  -Eh  [0|1]     High quality mode.\n"
		 "\nHQV/DQV common options:\n"
		 "  -Qc <name>     HQV/DQV channel name.\n"
		 "  -Qn <device>   HQV/DQV capture device number.\n"
		 "  -Qr [0|1]      HQV/DQV round seconds on filenames to '00'.\n"
		 "  -Qe [0|1]      HQV/DQV extended mode.\n"
		 "  -Qa [0|1]      HQV/DQV align time.\n"
		 "  -Qs [0|1]      HQV/DQV splice mode.\n"
		 "  -Qb <size>     HQV/DQV buffer size.\n"
		 "  -C  <chan>     Closed Captioning capture (when in DQV/HQV mode).\n" 
		 "  -S  <id>       DQV/HQV station ID.\n"
		 "  -e  <h-h>      Extended mode time range, default 1-5.\n"
		 "\nHQV options:\n"
		 "  -H             HQV mode, filenames use timestamps in /data/hqv/<channel>/<year>/<month>/<MMDDYY>/.\n"
		 "  -Ho <secs>     HQV offset seconds.\n"
		 "  -HK            HQV keep linked in /data/tmp for further processing.\n"
		 "  -HA [0|1]      HQV Archive in /data/hqv directory.\n"
		 "  -HR            HQV Remux MPEG2 to DVD with libavformat.\n"
		 "  -HN            HQV Remux MPEG2 to DVD with libavformat and normalize timestamps.\n"
		 "\nDQV options:\n"
		 "  -D             DQV mode, filenames use timestamps in /data/mpegout/.\n"
		 "\nCombine/Split Remux MPEG2/DVD options:\n"
		 "  -R             Remux MPEG2.\n"
		 "  -Rt            Remux MPEG2 and rewrite timestamps.\n"
		 "  -b <sec>       Start time to begin remuxing.\n"
		 "  -t <sec>       Total recording/remuxing time.\n"
		 "\nMisc options:\n"
		 "  -y             Overwrite output file if it exists.\n"
		 "  -an            No audio.\n"
		 "  -vn            No video.\n"
		 "  -sn            No subtitles.\n"
		 "  -d <level>     Debug level, -1 to 10, -1 quiet and 10 very verbose.\n" 
		 "  -n <level>     Nice level, -20 to 20, -20 highest and 20 lowest.\n" 
		 "  -l <sec>       Segment length.\n"
		 "  -r [0|1]       Raw streams when not muxed, turn on or off.\n"
		 "  -m <file>      Use MP4Box to Mux/Format A/V into MP4 container.\n"
		 "\n", argv[0]);

		if (longhelp) {
			printf("Long help options:\n"
		 		"  --formats              Show all formats/codecs supported.\n"
		 		"  --nosync               Don't duplicate video frames to sync A/V.\n"
		 		"  --resync               Fix timestamps when capture card output's them wrong\n"
		 		"  --ptsgen               Generate PTS/DTS from input video for encoding\n"
		 		"  --crop_left <int>      Video pixels to crop on left side of frame.\n"
		 		"  --crop_right <int>     Video pixels to crop on right side of frame.\n"
		 		"  --crop_top <int>       Video lines to crop on top side of frame.\n"
		 		"  --crop_bottom <int>    Video lines to crop on bottom side of frame.\n"
		 		"  --ssim <int>           Video turn on/off SSIM stats.\n"
		 		"  --psnr <int>           Video turn on/off PSNR stats.\n"
		 		"  --threads <int>        Video uses number threads [-1=auto, 0=none].\n"
		 		"  --hq [0|1]             Video high quality encoding mode.\n"
		 		"  --level <double>       Video h.264 level [0=auto].\n"
		 		"  --profile <type>       Video h.264 profile [baseline main high].\n"
		 		"  --cabac [0|1]          Video cabac off/on.\n"
		 		"  --deinterlace [0|1]    Video deinterlacing.\n"
		 		"  --interlace [0|1]      Video interlaced input and encoding output.\n"
		 		"  --nr <int>             Video noise reduction value.\n"
		 		"  --ofps <double>        Video output FPS.\n"
		 		"  --goplen <secs>        Video GOP length in seconds.\n"
		 		"  --gop <frames>         Video GOP length in frames.\n"
		 		"  --sws <0-10>           Video software scaling method.\n"
		 		"  --width <int>          Video frame width.\n"
		 		"  --height <int>         Video frame height.\n"
		 		"  --bitrate <int>        Video bitrate.\n"
		 		"  --maxrate <int>        Video RC maxrate.\n"
		 		"  --minrate <int>        Video RC minrate.\n"
		 		"  --bufsize <int>        Video RC buffer size.\n"
		 		"  --bt <ratio>           Video bitrate tolerance ratio.\n"
		 		"  --subme [1-10]         Video subme value.\n"
		 		"  --qcomp [0-1]          Video qcomp value [0=constant 1=variable].\n"
		 		"  --crf <1-51>           Video constant rate factor rc method.\n"
		 		"  --qsquish [0-1]        Video quantizer algorythm [0=cut 1=diff].\n"
		 		"  --aud [0|1]            Video aud off/on.\n"
		 		"  --partitions <types>   Video partitions [none|all] [i4x4,i8x8,p8x8,p4x4,b8x8].\n"
		 		"  --refs <int>           Video reference frames.\n"
		 		"  --mixed_refs [0|1]     Video mixed references off/on.\n"
		 		"  --bframes <0-16>       Video B frames.\n"
		 		"  --bstrategy <0-2>      Video B frame strategy.\n"
		 		"  --bpyramid [0|1]       Video bpyramid off/on.\n"
		 		"  --wpred [0|1]          Video wpred off/on.\n"
		 		"  --weightp [0|1|2]      Video weightp [0=off, 1=fast, 2=smart].\n"
		 		"  --fastpskip [0|1]      Video fastpskip off/on.\n"
#if defined(MBTREE)
		 		"  --mbtree [0|1]         Video MB tree RC off/on.\n"
		 		"  --lookahead [0-255]    Video MB tree RC lookahead frames.\n"
#endif
#if defined(PSYRD)
		 		"  --psy_rd <double>      Video psy_rd value.\n"
		 		"  --psy_trellis <double> Video psy_trellis value.\n"
		 		"  --aq <0-2>             Video AQ Mode [0=off 1=AQ 2=VAQ].\n"
		 		"  --aq_strength <double> Video AQ strength.\n"
#endif
#if defined(SLICES)
		 		"  --slices <int>         Video x264 encoded slices per frame [0=off, N=slice count].\n"
#endif
		 		"  --trellis [0-2]        Video trellis [0=off 1=on 2=aggressive].\n"
		 		"  --nodeblock [0|1]      Video deblocking on/off.\n"
		 		"  --deblocka <int>       Video deblocking alpha.\n"
		 		"  --deblockb <int>       Video deblocking beta.\n"
		 		"  --scthreshold <int>    Video scene change thresh hold value.\n"
		 		"  --me_method <method>   Video motion estimation method [zero full epzs hex umh iter tesa].\n"
		 		"  --me_range <int>       Video motion estimation range.\n"
		 		"  --chroma_me <int>      Video motion estimation uses chroma.\n"
		 		"  --directpred [0-3]     Video direct prediction mode [0=none 1=spatial 2=temporal 3=auto].\n"
		 		"  --muxrate <int>        Video muxrate.\n"
		 		"  --packetsize <int>     Video packetsize.\n"
		 		"  --audio_quality <int>  Audio quality VBR.\n"
		 		"  --abitrate <int>       Audio bitrate.\n"
		 		"  --channels <int>       Audio channels.\n"
		 		"  --rate <int>           Audio sample rate.\n"
		 		"  --title <string>       Tagged Title of video/audio.\n"
		 		"  --author <string>      Tagged Author of video/audio.\n"
		 		"  --copyright <string>   Tagged Copyright of video/audio.\n"
		 		"  --comment <string>     Tagged Comment of video/audio.\n"
		 		"  --album <string>       Tagged Album of video/audio.\n"
				"\n");
		} 
		exit(1);
	}
	
	// Signal handlers
	signal(SIGINT,sighdlr);
	signal(SIGQUIT,sighdlr);
	signal(SIGTERM,sighdlr);
	signal(SIGPIPE,sighdlr);
	signal(SIGALRM,sighdlr);
	
	// Log level and debugging
	if (debug >= 3)
		av_log_set_level(AV_LOG_DEBUG);
	else if (debug == 2)
		av_log_set_level(AV_LOG_VERBOSE);
	else if (debug == 1)
		av_log_set_level(AV_LOG_INFO);
	else if (debug == 0)
		av_log_set_level(AV_LOG_WARNING);
	else if (debug == -1)
		av_log_set_level(AV_LOG_ERROR);
	else
		av_log_set_level(AV_LOG_FATAL);

	if (remux_timestamps)
		encode = 0;

	// Show status output
	if (debug == 1)
		show_status = 1;
	
	if (show_status) {
		sprintf(slog, "%s", "\r");
		sprintf(elog, "%s", "     ");
	}

	// Check if input/output files given
	if (!i_filename) {
		fprintf(stderr, "Error, no input file given with -i arg.\n");
		exit(1);
	} else if (rawyuv == NULL && rawpcm == NULL && rawsub == NULL && !cs->mux && !hqvData->do_dqv && !hqvData->do_hqv && !hqvData->do_dvd) {
		fprintf(stderr, "Error, no output file given with -a -v or -o args.\n");
		exit(1);
	}
	
	// Read Codec File
	for(i=0; i < num_codec_files; i++) {
		if (codec_files[i] != NULL)
			if (read_codec(cs, codec_files[i]))
				return -1;
	}
	
	// Setup Codec Structure
	init_codec(cs);
	
	// Set priority to the highest possible
	if (nice(nice_level) != nice_level)
		fprintf(stderr, "Warning: couldn't set nice level to %d.\n", nice_level);
	setpriority(0, PRIO_PROCESS, nice_level);
	
	// Input File Pipe?
	if (!strcmp(i_filename, "-"))
		i_filename = "pipe:";
	else if (!strcmp(i_filename, "http:"))
		ds.streaming = 1;
	
	// Check if file exists, ask about overwrite if not specified and not stdin for input
	if (o_filename && !do_overwrite && strcmp(i_filename, "http:") && strcmp(i_filename, "pipe:") && 
		!hqvData->do_dqv && !hqvData->do_hqv && !hqvData->do_dvd) 
	{
		struct stat sb;
	
		if (stat(o_filename, &sb) != -1) {
			char c = 'n';
			fprintf(stderr, "Warning, file %s exists, overwrite? [y/n]: ", o_filename);
			if (fread(&c, 1, 1, stdin) != 1)
				exit(1);
			if (c != 'y')
				exit(1);
		}
	}
	
	// Default channel name if not specified
	if (!hqvData->channel) {
		char fbuf[10] = "";
		hqvData->channel = fbuf;
		sprintf(hqvData->channel, "%s%02d", 
			"UNKNOWN", hqvData->device);
	}

	// Time Align
	if (hqvData->do_hqv || hqvData->do_dqv || hqvData->do_dvd) {
		char db[30];

		// Logging through syslog with av_log callback
		open_log(hqvData->device, 1);

		// Print out information header
		av_log(NULL, AV_LOG_WARNING, "Starting NGTC %s %s/%s Capture on device [%d] channel [%s] input [%s].\n"
				" Segment time [%d] seconds\n Offset [%d] minutes\n Buffer size [%d] bytes\n Splice mode [%s]\n"
				" DQV mode [%s]\n HQV mode [%s]\n DVD mode [%s]\n Extended mode [%s]\n CC Capture [%s]\n",
				NGTC_VERSION, hqvData->do_hqv?"HQV":"", hqvData->do_dqv?"DQV":"", 
				hqvData->device, hqvData->channel, i_filename, 
				hqvData->seconds, hqvData->offset, hqvData->read_size, hqvData->splice?"on":"off",
				hqvData->do_dqv?"on":"off", hqvData->do_hqv?"on":"off", hqvData->do_dvd?"on":"off", hqvData->em?"on":"off",
				hqvData->cc?"on":"off");

		// Print out information header
		fprintf(stderr, "%s: Starting NGTC %s %s/%s Capture on device [%d] channel [%s] input [%s].\n"
				" Segment time [%d] seconds\n Offset [%d] minutes\n Buffer size [%d] bytes\n Splice mode [%s]\n"
				" DQV mode [%s]\n HQV mode [%s]\n DVD mode [%s]\n Extended mode [%s]\n CC Capture [%s]\n",
				current_date(db), NGTC_VERSION, hqvData->do_hqv?"HQV":"", hqvData->do_dqv?"DQV":"", 
				hqvData->device, hqvData->channel, i_filename, 
				hqvData->seconds, hqvData->offset, hqvData->read_size, hqvData->splice?"on":"off",
				hqvData->do_dqv?"on":"off", hqvData->do_hqv?"on":"off", hqvData->do_dvd?"on":"off", hqvData->em?"on":"off",
				hqvData->cc?"on":"off");
		
		if (hqvData->do_hqv || hqvData->do_dvd) {
			mkdir(HQV_DIR,0755);
			mkdir(HQV_TMP,0755);
		}
		if (hqvData->do_dqv)
			mkdir(DQV_DIR,0755);
		
		// Closed Caption 
		if (hqvData->cc) {
			char cmdline[512] = "";

			sprintf(cmdline, "/usr/bin/nohup /usr/local/bin/capture -v -a %d -c -p -d /dev/vbi%d -i %d -T %d -W >>/var/log/cclog%d.log 2>&1>/dev/null &", 
				hqvData->align, hqvData->device, hqvData->stationid, hqvData->cc, hqvData->device);

			if (debug >= 0)
				av_log(NULL, AV_LOG_WARNING, "Launching CC Capture:\n %s\n", cmdline);

			if (system(cmdline) == -1)
				av_log(NULL, AV_LOG_ERROR, "Error: Launching CC Capture Failed:\n");

		}
		
		// Align to segment time
		if (hqvData->align)
			align_time(hqvData->seconds, hqvData->offset);

		// User sent signal to exit
		if (!continueDecoding)
			exit(0);

		// Save input filename as source for capture
		sprintf(hqvData->source, "%s", i_filename);

		// Input fifo allocation
		if (hqvData->do_dqv || hqvData->do_dvd)
			hqvData->fifo = av_fifo_alloc(INPUT_FIFO_SIZE);

		// Create and launch HQV Thread
		hqvHandle = pthread_create( &hqvThread,
			   NULL, hqv_thread, (void*) hqvData);

		// Wait for HQV if we aren't doing DQV
		if (!hqvData->do_dqv && !hqvData->do_dvd) {
			pthread_join(hqvThread, NULL);
			continueDecoding = 0;

			while (!hqvData->finished) 
				usleep(250000);

			// End of program in HQV only mode
			free(hqvData);
			exit(0);
		}

		if (hqvData->do_dvd && !hqvData->do_dqv)
			encode = 0;

		ds.streaming = 1;

		// Generate PTS/DTS
		do_ptsgen = 1;
	}

	// Start timer
	starttime = av_gettime() / 1000000;

	/* initialize libavcodec, and register all codecs and formats */
	avcodec_register(x264_enc);
	av_register_all();
	avfilter_register_all();
	
	// Input Format Parameters
	memset(ap, 0, sizeof(*ap));
	ap->prealloced_context = 1;
	ap->sample_rate = input_sample_rate;
	ap->channels = input_channels;
	ap->time_base.den = input_time_base_num;
	ap->time_base.num = input_time_base_den;
	ap->width = input_width;
	ap->height = input_height;
	ap->pix_fmt = input_pix_fmt;

	// Video Codec detection
	/*if (input_video_codec != NULL) {
		AVCodec *codec;
		codec = avcodec_find_decoder_by_name(input_video_codec);
		if(!codec) {
			av_log(NULL, AV_LOG_FATAL, "Unknown Video Decoding Codec %s\n", input_video_codec);
			exit(1);
		}
		ap->video_codec_id = codec->id;
		//ds.pFormatCtx->video_codec_id = codec->id;
		av_log(NULL, AV_LOG_INFO, "Video Decoding Codec %s\n", codec->name);
	} else
		ap->video_codec_id = 0;

	// Audio Codec detection
	if (input_audio_codec != NULL) {
		AVCodec *codec;
		codec = avcodec_find_decoder_by_name(input_audio_codec);
		if(!codec) {
			av_log(NULL, AV_LOG_FATAL, "Unknown Audio Decoding Codec %s\n", input_audio_codec);
			exit(1);
		}
		ap->audio_codec_id = codec->id;
		//ds.pFormatCtx->audio_codec_id = codec->id;
		av_log(NULL, AV_LOG_INFO, "Audio Decoding Codec %s\n", codec->name);
	} else
		ap->audio_codec_id = 0;*/

	ds.ap = ap;
	ds.input_format = input_format;
	
	// Setup Demuxer Format
	if (setupDemuxerFormat(&ds, i_filename, hqvData) < 0)
		exit(1);

	ds.fmt = infmt;
	pFormatCtx = ds.pFormatCtx;
	
	// Subtitle Codec detection
	if (input_sub_codec != NULL) {
		AVCodec *codec;
		codec = avcodec_find_decoder_by_name(input_sub_codec);
		if(!codec) {
			av_log(NULL, AV_LOG_FATAL, "Unknown Subtitle Decoding Codec %s\n", input_sub_codec);
			exit(1);
		}
		pFormatCtx->subtitle_codec_id = codec->id;
		av_log(NULL, AV_LOG_INFO, "Subtitle Decoding Codec %s\n", codec->name);
	}

	// Discover all streams in media file and map ones we want to decode
	videoStream = audioStream = subStream = -1;
	for (i = 0; i < pFormatCtx->nb_streams; i++) {
		switch(pFormatCtx->streams[i]->codec->codec_type) {
			case CODEC_TYPE_AUDIO:
				if (audioStream == -1)
					audioStream = i;
				if (!no_audio)
					cs->do_audio = 1;
				else
					pFormatCtx->streams[i]->discard= AVDISCARD_ALL;
				break;
			case CODEC_TYPE_VIDEO:
				if (videoStream == -1)
					videoStream = i;
				if (!no_video)
					cs->do_video = 1;
				else
					pFormatCtx->streams[i]->discard= AVDISCARD_ALL;
				break;
			case CODEC_TYPE_SUBTITLE:
				if (subStream == -1)
					subStream = i;
				if (!no_sub)
					cs->do_sub = 1;
				else
					pFormatCtx->streams[i]->discard= AVDISCARD_ALL;
				break;
			case CODEC_TYPE_DATA:
			case CODEC_TYPE_ATTACHMENT:
			case CODEC_TYPE_UNKNOWN:
			default:
				pFormatCtx->streams[i]->discard= AVDISCARD_ALL;
				break;
		}
	}
	
	if (videoStream == -1 && audioStream == -1 && subStream == -1) {
		av_log(NULL, AV_LOG_FATAL, 	
				"Error: didn't find any video/audio streams.\n");
		exit(1);	// Didn't find a video stream
	}
	
	if (audioStream == -1)
		cs->do_audio = 0;
	if (videoStream == -1)
		cs->do_video = 0;
	if (subStream == -1)
		cs->do_sub = 0;

	ds.do_video = cs->do_video;
	ds.do_audio = cs->do_audio;
	ds.do_sub = cs->do_sub;
	ds.videoStream = videoStream;
	ds.audioStream = audioStream;
	ds.subStream = subStream;
	
	// Setup Demuxer Codecs
	if (setupDemuxerCodecs(&ds) < 0)
		exit(1);

	pCodecCtxVideo = ds.pCodecCtxVideo;
	pCodecCtxAudio = ds.pCodecCtxAudio;
	pCodecCtxSub = ds.pCodecCtxSub;

	// Dump information about file onto standard error
	if (debug > 0)
		dump_format(pFormatCtx, 0, i_filename, false);

	// File Segments
	if (hqvData->seconds > 0) {
		if (cs->mux && !hqvData->do_dqv && !hqvData->do_dvd) {
			// Mux output filename
			if (o_filename != NULL 
				&& ((cs->do_video && rawyuv == NULL) 
					|| (cs->do_audio && rawpcm == NULL))) 
			{
				sprintf(VideoFile, "%s", o_filename);
				sprintf(AudioFile, "%s", o_filename);
				sprintf(SubFile, "%s", o_filename);
				sprintf(OutputFile, "%s", o_filename);
				
				if (cs->do_video && rawyuv == NULL)
					rawyuv = VideoFile;
				if (cs->do_audio && rawpcm == NULL)
					rawpcm = AudioFile;
				//if (cs->do_sub && rawsub == NULL)
				//	rawsub = SubFile;
				
				o_filename = OutputFile;
			} 
		}

		if (cs->do_video) {
			if (((!hqvData->do_dqv && !hqvData->do_dvd) || (!cs->mux && !hqvData->do_dvd)) && rawyuv != NULL)
				if (split_string(rawyuv, vout_filename_base, vext, '.'))
					exit(1);
			
			if (!cs->mux && hqvData->record && !hqvData->do_dvd) {
				if (hqvData->do_dqv) {
					dqv_get(VideoFile, cs->hostname, hqvData->channel, hqvData->device, vext, hqvData->round, DQV_DIR);
					av_log(NULL, AV_LOG_INFO, "Video Filename: %s\n", VideoFile);
				} else
					sprintf(VideoFile, "%s-%04d.%s", vout_filename_base, video_segment_count, vext);

				rawyuv = VideoFile;
			} else if (!hqvData->record || hqvData->do_dvd)
				rawyuv = NULL;
		}
		
		if (cs->do_audio) {
			if (((!hqvData->do_dqv && !hqvData->do_dvd) || (!cs->mux && !hqvData->do_dvd)) && rawpcm != NULL) 
				if (split_string(rawpcm, aout_filename_base, aext, '.'))
					exit(1);
			
			if (!cs->mux && hqvData->record && !hqvData->do_dvd) {
				if (hqvData->do_dqv) {
					dqv_get(AudioFile, cs->hostname, hqvData->channel, hqvData->device, aext, hqvData->round, DQV_DIR);
					av_log(NULL, AV_LOG_INFO, "Audio Filename: %s\n", AudioFile);
				} else
					sprintf(AudioFile, "%s-%04d.%s", aout_filename_base, audio_segment_count, aext);

				rawpcm = AudioFile;
			} else if (!hqvData->record || hqvData->do_dvd)
				rawpcm = NULL;
		}
		
		if (cs->mux || mux_in_thread) {
			if (o_filename != NULL)
				split_string(o_filename, mout_filename_base, mext, '.');
			else if (cs->do_video && rawyuv != NULL)
				split_string(rawyuv, mout_filename_base, mext, '.');
			else if (cs->do_audio && rawpcm != NULL)
				split_string(rawpcm, mout_filename_base, mext, '.');
			else {
				av_log(NULL, AV_LOG_FATAL, "Error: No filename to use for Mux File.\n");
				exit(1);
			}
			
			if (hqvData->record) {
				if (hqvData->do_dqv) {
					dqv_get(OutputFile, cs->hostname, hqvData->channel, hqvData->device, mext, hqvData->round, DQV_DIR);
					av_log(NULL, AV_LOG_INFO, "\rMUX Filename: %s\n", OutputFile);
				} else
					sprintf(OutputFile, "%s-%04d.%s", mout_filename_base, 1, mext);

				o_filename = OutputFile;
			} else
				o_filename = NULL;
		}
		
		if (debug >= 0) {
			av_log(NULL, AV_LOG_WARNING, "Segmenting files for %d seconds", hqvData->seconds);
			if (!cs->mux && !hqvData->do_dvd) {
				if (cs->do_video)
					av_log(NULL, AV_LOG_WARNING, " video: %s", rawyuv);
				if (cs->do_audio)
					av_log(NULL, AV_LOG_WARNING, " audio: %s", rawpcm);
				if (mux_in_thread)
					av_log(NULL, AV_LOG_WARNING, " muxfile: %s", o_filename);
			} else if (!hqvData->do_dvd) {
				av_log(NULL, AV_LOG_WARNING, " A/V Mux file: %s", o_filename);
			}
			av_log(NULL, AV_LOG_WARNING, "\n");
		}
	}

	// Check if we format 2 separate streams
	if (!do_raw && cs->do_video && cs->do_audio && !cs->mux)
		cs->do_demux = 1;
	
	// Video encoding codec detection
	if (cs->video_codec) {
		AVCodec *codec;
		codec = avcodec_find_encoder_by_name(cs->video_codec);
		if(!codec) {
			av_log(NULL, AV_LOG_FATAL, "Unknown Video Encoding Codec %s\n", cs->video_codec);
			exit(1);
		}
		cs->video_codec_id = codec->id;
	}
	
	// Audio encoding codec detection
	if (cs->audio_codec) {
		AVCodec *codec;
		codec = avcodec_find_encoder_by_name(cs->audio_codec);
		if(!codec) {
			av_log(NULL, AV_LOG_FATAL, "Unknown Audio Encoding Codec %s\n", cs->audio_codec);
			exit(1);
		}
		cs->audio_codec_id = codec->id;
	}
	
	// Start Muxer
	if (encode)
		if(init_muxer(cs, o_filename))
			exit(1);
	
	// Get a pointer to the codec context for the video stream
	if (cs->do_video) {
		// Open Video file
		if (!cs->mux && !cs->do_demux && rawyuv) {
			vFile = fopen(rawyuv, "wb");
			if (vFile == NULL) {
				av_log(NULL, AV_LOG_FATAL, 	
					"Error: Unable to open video output file.\n");
				exit(1);
			}
		}
		// Video Input FPS
		in_fps = av_q2d(pFormatCtx->streams[videoStream]->r_frame_rate);
		
		// Video Output FPS
		if (cs->out_fps <= 0)
			cs->out_fps = in_fps;
		cs->ofps = av_d2q(cs->out_fps, 60000);
		
		// Video Width and Height
		in_height = pCodecCtxVideo->height;
		in_width = pCodecCtxVideo->width;
		
		// Video SAR calculation
		calc_sar(cs, in_width, in_height, pCodecCtxVideo->sample_aspect_ratio);
		
		// Get original bitrate if rate control not given
		if (cs->bitrate == 0 && cs->crf == 0 && pCodecCtxVideo->bit_rate)
			cs->bitrate = pCodecCtxVideo->bit_rate;
		
		// Video input format
		in_vfmt = pCodecCtxVideo->pix_fmt;
		
		// Allocate decoder output raw video frame
		avcodec_get_frame_defaults(&pFrame);
		if (avpicture_alloc((AVPicture*)&pFrame, pCodecCtxVideo->pix_fmt, in_width, in_height)) {
			av_log(NULL, AV_LOG_FATAL, "Could not allocate pFrame\n");
			exit(1);
		}

		// Verify cropping is divisible by 2
		if (cs->crop_left || cs->crop_right || cs->crop_top || cs->crop_bottom) {
			if ((cs->crop_left || cs->crop_right) && (cs->crop_left+cs->crop_right)%2 != 0) {
				av_log(NULL, AV_LOG_FATAL, "Cropping size must be divisible by 2, left/right values are an odd number.\n");
				exit(1);
			}
			if ((cs->crop_top || cs->crop_bottom) && (cs->crop_top+cs->crop_bottom)%2 != 0) {
				av_log(NULL, AV_LOG_FATAL, "Cropping size must be divisible by 2, top/bottom values are an odd number.\n");
				exit(1);
			}
				
		}

		// Allocate decoder raw cropped video frame
		avcodec_get_frame_defaults(&pFrameCrop);
		if (avpicture_alloc((AVPicture*)&pFrameCrop, pCodecCtxVideo->pix_fmt, in_width-(cs->crop_left), in_height-(cs->crop_top))) {
			av_log(NULL, AV_LOG_FATAL, "Could not allocate pFrameCrop\n");
			exit(1);
		}
		
		// Get video stream start time
		if (pFormatCtx->streams[videoStream]->start_time >= 0)
			AVTimestamp.vstart = AVTimestamp.rstart = pFormatCtx->streams[videoStream]->start_time;
		else
			AVTimestamp.vstart = AVTimestamp.rstart = -1;

		// Video Encoder setup
		if (encode) {
			// Setup Encoder with Codec Settings
			if(init_video_encoder(cs))
				exit(1);

        		// Check if we only need to copy the video
        		if (!vfilters && !cs->deinterlace && cs->video_codec_id == pCodecCtxVideo->codec_id &&
                		cs->out_fps == in_fps && cs->h == pCodecCtxVideo->height &&
                		cs->w == pCodecCtxVideo->width && (cs->crop_left+cs->crop_right+cs->crop_top+cs->crop_bottom) == 0 &&
                		cs->bitrate == pCodecCtxVideo->bit_rate) {
                       			cs->copy_video = 1;
                       			av_log(NULL, AV_LOG_WARNING, "\rVideo will be copied since same input as output\n");
        		}

			// Open Encoder
			if(open_video_encoder(cs))
				exit(1);
			
			video_outbuf_size = 256 * 1024;
			video_outbuf_size= FFMAX(video_outbuf_size, 6*(cs->w*cs->h) + 200);
			video_outbuf = (uint8_t *)av_malloc(video_outbuf_size);
		}

		/* allocate the scaler output/encoder input raw picture */
		avcodec_get_frame_defaults(&picture);
		if (avpicture_alloc((AVPicture*)&picture, encode?cs->vEncCtx->pix_fmt:pCodecCtxVideo->pix_fmt, cs->w, cs->h)) {
			av_log(NULL, AV_LOG_FATAL, "Could not allocate picture\n");
			exit(1);
		}

		// Scale context
		/*if (!do_vfilter) {
			img_convert_ctx = sws_getContext(in_width-(cs->crop_left+cs->crop_right), in_height-(cs->crop_top+cs->crop_bottom), in_vfmt,
				 cs->w, cs->h, encode?cs->vEncCtx->pix_fmt:pCodecCtxVideo->pix_fmt,
				 cs->sws_flags, NULL, NULL, NULL);

			if (img_convert_ctx == NULL) {
				av_log(NULL, AV_LOG_FATAL, "\rCannot initialize the video conversion context\n");
				exit(1);
			}
		}*/

		// Video timebase
		tbn_v =
			pFormatCtx->streams[videoStream]->time_base.den /
				pFormatCtx->streams[videoStream]->time_base.num;
		
		// Video frame duration
		v_dur_in = ((double)pFormatCtx->streams[videoStream]->time_base.den/(double)pFormatCtx->streams[videoStream]->time_base.num) /
				((double)pFormatCtx->streams[videoStream]->r_frame_rate.num/(double)pFormatCtx->streams[videoStream]->r_frame_rate.den);

		v_dur_out = ((double)pFormatCtx->streams[videoStream]->time_base.den/(double)pFormatCtx->streams[videoStream]->time_base.num) /
				((double)cs->ofps.num/(double)cs->ofps.den);

		// Print out video information
		av_log(NULL, AV_LOG_VERBOSE, "Video Input FMT: %d Outbuf size: %d Output FMT: %d\n", in_vfmt, video_outbuf_size, encode?cs->vEncCtx->pix_fmt:in_vfmt);
		
		av_log(NULL, AV_LOG_VERBOSE, "Video Input FPS: %02.3f Output FPS: %02.3f\n", in_fps, cs->out_fps);

		av_log(NULL, AV_LOG_VERBOSE, "Video tbr: %d/%d tbn: %d/%d tbc: %d/%d\n", 
				pFormatCtx->streams[videoStream]->r_frame_rate.num, pFormatCtx->streams[videoStream]->r_frame_rate.den,
				pFormatCtx->streams[videoStream]->time_base.den, pFormatCtx->streams[videoStream]->time_base.num,
				pFormatCtx->streams[videoStream]->codec->time_base.den, pFormatCtx->streams[videoStream]->codec->time_base.num);

		av_log(NULL, AV_LOG_VERBOSE, "Video Time Base: %"PRId64" Input Frame Duration: %0.2f Output Frame Duration: %0.2f\n", 
				tbn_v, v_dur_in, v_dur_out);
	} else {
		AVTimestamp.vstart = cs->out_fps = in_fps = 0;
	}

	if (cs->do_audio) {
		// Open Audio file
		if (!cs->mux && !cs->do_demux && rawpcm) {
			aFile = fopen(rawpcm, "wb");
			if (aFile == NULL) {
				av_log(NULL, AV_LOG_FATAL, 	
					"Error: Unable to open audio output file.\n");
				exit(1);
			}
		}

		// Audio channels
		in_achan = pCodecCtxAudio->channels;
		if (cs->achan == -1)
			cs->achan = in_achan;
		// Audio rate
		in_arate = pCodecCtxAudio->sample_rate;
		if (cs->arate == -1)
			cs->arate = in_arate;
		// Sample input format
		in_afmt = pCodecCtxAudio->sample_fmt;
		// Sample output format
		cs->sample_fmt = out_afmt = pCodecCtxAudio->sample_fmt;
		// Channel layout
		cs->channel_layout = pCodecCtxAudio->channel_layout;
		// Profile
		cs->audio_profile = pCodecCtxAudio->profile;
		// Audio bitrate
		if (cs->abitrate == 0 && cs->audio_quality == 0) {
			if (pCodecCtxAudio->bit_rate > 1 && pCodecCtxAudio->bit_rate <= 448000)
				cs->abitrate = pCodecCtxAudio->bit_rate;
			else
				cs->abitrate = 384000; // no bitrate specified
		}
		
		// Get audio stream start time
		if (pFormatCtx->streams[audioStream]->start_time >= 0) {
			if (AVTimestamp.rstart == -1
				|| AVTimestamp.rstart > pFormatCtx->streams[audioStream]->start_time) {
				AVTimestamp.astart = AVTimestamp.rstart =
					pFormatCtx->streams[audioStream]->start_time;
			} else
				AVTimestamp.astart = pFormatCtx->streams[audioStream]->start_time;
		} else
			AVTimestamp.astart  = -1;
		
		tbn_a =
			pFormatCtx->streams[audioStream]->time_base.den /
			pFormatCtx->streams[audioStream]->time_base.num;
		
		av_log(NULL, AV_LOG_VERBOSE, "Audio tbr: %d/%d tbn: %d/%d tbc: %d/%d\n", 
				pFormatCtx->streams[audioStream]->r_frame_rate.num, pFormatCtx->streams[audioStream]->r_frame_rate.den,
				pFormatCtx->streams[audioStream]->time_base.den, pFormatCtx->streams[audioStream]->time_base.num,
				pFormatCtx->streams[audioStream]->codec->time_base.den, pFormatCtx->streams[audioStream]->codec->time_base.num);

		// Audio Encoder setup
		if (encode) {
			if (init_audio_encoder(cs))
				exit(1);

			// Check if we only need to copy the audio
			if (cs->audio_codec_id == pCodecCtxAudio->codec_id && 
				cs->achan == in_achan && cs->arate == in_arate && 
				cs->abitrate == pCodecCtxAudio->bit_rate) {
					cs->copy_audio = 1;
					av_log(NULL, AV_LOG_WARNING, "\rAudio will be copied since same input as output\n");
			}

			if (open_audio_encoder(cs))
				exit(1);
			
			// Audio Encoder Buffer
			audio_frame_size = cs->aEncCtx->frame_size;
			audio_outbuf_size = 4*MAX_AUDIO_PACKET_SIZE;
			audio_outbuf = av_malloc(audio_outbuf_size);
			
			out_afmt = cs->aEncCtx->sample_fmt;
		}
		av_log(NULL, AV_LOG_VERBOSE, "Audio encoder frame size: %d decoder frame size: %d\n", 
			audio_frame_size, pCodecCtxAudio->frame_size);

		// Audio Decoder Buffer
		audio_buf_size = (AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2;
		audio_buf_out = (uint8_t*)av_malloc(audio_buf_size);
		audio_buf_out_rs = (uint8_t*)av_malloc(audio_buf_size);
		
		// Audio Fifo Buffer
		audio_fifo = av_fifo_alloc(2*MAX_AUDIO_PACKET_SIZE);

		av_log(NULL, AV_LOG_VERBOSE, "Audio Time Base: %"PRId64" input rate: %d channels: %d output rate: %d channels: %d\n",
			tbn_a, in_arate, in_achan, cs->arate, cs->achan);

		// Audio Resampler
		if (cs->achan != in_achan || cs->arate != in_arate) {
			av_convert_ctx = av_audio_resample_init(cs->achan, in_achan,
				cs->arate, in_arate,
				out_afmt, in_afmt,
				16, 10, 0, 0.8);
		
			if (av_convert_ctx == NULL) {
				av_log(NULL, AV_LOG_FATAL, "Cannot initialize the audio resampler.\n");
				exit(1);
			}
		}
	} else {
		AVTimestamp.astart = 0;
	}
	
	if (cs->do_sub) {
		// Open Subtitle file
		if (/*!cs->mux && !cs->do_demux &&*/ rawsub) {
			sFile = fopen(rawsub, "wb");
			if (sFile == NULL) {
				av_log(NULL, AV_LOG_FATAL, 	
					"Error: Unable to open subtitle output file.\n");
				exit(1);
			}
		}
	}
	
	// Mux Thread
	if (mux_in_thread) {
		muxData = (struct mux_data *) malloc(sizeof(struct mux_data));
		
		memset(muxData, 0, sizeof(struct mux_data));
		
		muxData->fps = cs->out_fps;
		muxData->muxReady = 0;
		muxData->count = 0;
		muxData->do_audio = cs->do_audio;
		muxData->do_video = cs->do_video;
		
		muxHandle = pthread_create( &muxThread,
								   NULL, mux_thread, (void*) muxData);
	}
	
	// Start Muxer
	if (encode && !cs->do_demux) {
		if (start_muxer(cs, o_filename))
			goto failure;

		if (debug > 0)
			dump_format(cs->oc, 0, o_filename, 1);
	}
	
	// Video Stream Start Time
	if (cs->do_video && AVTimestamp.vstart >= 0 && AVTimestamp.rstart >= 0 && AVTimestamp.vstart >= AVTimestamp.rstart)
		AVTimestamp.vlastdts = AVTimestamp.vstart - AVTimestamp.rstart;
	else
		AVTimestamp.vlastdts = 0;

	// Copy Codec Structure
	memcpy(&streams[1], &streams[0], sizeof(struct CodecSettings));

	if (cs->do_demux) {
		csv = &streams[0];
		csa = &streams[1];

		stop_muxer(csv);
		csv->aEncCtx = NULL;
		csv->vEncCtx = NULL;
		csa->aEncCtx = NULL;
		csa->vEncCtx = NULL;

		// Setup Video Mux File
		if(init_muxer(csv, rawyuv))
			goto failure;
		if(cs->do_video && (init_video_encoder(csv) || open_video_encoder(csv)))
			goto failure;
		if(start_muxer(csv, rawyuv))
			goto failure;

		if (debug > 0)
			dump_format(csv->oc, 0, rawyuv, 1);

		// Setup Audio Mux File
		if(init_muxer(csa, rawpcm))
			goto failure;
		if(cs->do_audio && (init_audio_encoder(csa) || open_audio_encoder(csa)))
			goto failure;
		if(start_muxer(csa, rawpcm))
			goto failure;

		if (debug > 0)
			dump_format(csa->oc, 1, rawpcm, 1);
	}
	
	// Check if inside/outside Extended Mode
	if (!hqvData->record)
		do_record = do_record_dvd = 0;

	// If user wants to stop program
	if (!continueDecoding)
		goto failure;

	// Initialize DVD muxing
	if (do_record_dvd && hqvData->do_dvd && hqvData->output
		&& (!(pMPEGVideoFormatCtx = pMPEGAudioFormatCtx = init_dvdmux(cs, 
				pFormatCtx, videoStream, audioStream, hqvData->output)) 
					|| !pMPEGVideoFormatCtx)) {
			av_log(NULL, AV_LOG_FATAL, "\rError opening dvd muxer\n");
			exit(1);
	} else if (remux_timestamps && (!(pMPEGVideoFormatCtx = pMPEGAudioFormatCtx = init_dvdmux(cs, 
		pFormatCtx, videoStream, audioStream, o_filename)) 
			|| !pMPEGVideoFormatCtx)) {
			av_log(NULL, AV_LOG_FATAL, "\rError opening dvd muxer\n");
			exit(1);
	} else if (hqvData->do_dvd && do_record_dvd && hqvData->archive) {
		if (link(hqvData->output, hqvData->output_tmp)) {
			av_log(NULL, AV_LOG_FATAL, "\rError linking hqv output file\n");
			exit(1);
		}
			
	}

	if (hqvData->do_dvd && do_record_dvd && debug > 0)
		dump_format(pMPEGVideoFormatCtx, 0, hqvData->output, 1);
	else if (remux_timestamps && debug > 0)
		dump_format(pMPEGVideoFormatCtx, 0, o_filename, 1);

	// Setup filters
	if (do_vfilter && csv->do_video) {
		ist->st = pFormatCtx->streams[videoStream];
		if (csv->oc)
			ost->st = csv->oc->streams[0];

		ost->sws_opts = csv->sws_flags;
		ost->video_crop = (csv->crop_top || csv->crop_bottom || csv->crop_left || csv->crop_right);
		ost->topBand = csv->crop_top;
		ost->leftBand = csv->crop_left;
		ost->frame_topBand = csv->crop_top;
		ost->frame_bottomBand = csv->crop_bottom;
		ost->frame_leftBand = csv->crop_left;
		ost->frame_rightBand = csv->crop_right;

		ist->out_video_filter = NULL;
		ist->input_video_filter = NULL;

		if (configure_filters(graph, vfilters, ist, ost, csv)) {
			av_log(NULL, AV_LOG_FATAL, "\rError configuring filters %s\n", vfilters?vfilters:"NULL");
			exit(1);
		}
	}

	/*********************/
	/* Main Program Loop */
	/*********************/
	for(;;) {
		int exit_loop = 0;
		int got_packet;

		// Things are stuck
		if (hqvData->fifo_full) {
			av_log(NULL, AV_LOG_WARNING, "HQV Fifo is full, DQV Thread signaled to exit\n");
			break;
		}
		
		// Get next demuxed packet
		if (continueDecoding || hqvData->do_dvd || hqvData->do_dqv) {
			got_packet = av_read_frame(pFormatCtx, &packet);

			// Failed at getting packet
			if (got_packet < 0) {
				int trys = 0;

				av_free_packet(&packet);

				//if (got_packet == AVERROR(EPIPE))
				//	break;

				if (got_packet == AVERROR(EPIPE) || got_packet == AVERROR(EIO))
					av_log(NULL, AV_LOG_VERBOSE, 
						"\rEOF for demuxing input\n");
				else
					av_log(NULL, AV_LOG_WARNING, 
						"\rFailed demuxing input, read_frame returned %d.\n", 
							got_packet);

				// Try again
				while (got_packet == AVERROR(EAGAIN)) {
					got_packet = av_read_frame(pFormatCtx, &packet);
					if (trys++ > 100 || got_packet >= 0) {
						av_free_packet(&packet);
						break;
					}

					av_log(NULL, AV_LOG_WARNING, 
						"\rTry %d failed demuxing input, read_frame returned %d.\n", 
							trys, got_packet);

					av_free_packet(&packet);
				}

				// Total Failure
				if (trys && got_packet < 0) {
					av_log(NULL, AV_LOG_WARNING, 
						"\rGave up demuxing input after %d trys, read_frame returned %d.\n", 
							trys, got_packet);
				}
			}
		} else
			got_packet = -1;

		// End of input packets in DVD mode
		if (got_packet < 0 && hqvData->do_dvd && !hqvData->do_dqv)
			last_frame = 1;

		// Combining and remuxing multiple input files
		if (got_packet < 0 && input_file_pos+1 < num_input_files) {
			input_file_pos++;

			// Close previous input file
			freeDemuxer(&ds);

        		// Setup Demuxer for next input file
        		if (setupDemuxerFormat(&ds, input_files[input_file_pos], hqvData) < 0)
                		exit(1);

        		pFormatCtx = ds.pFormatCtx;

			// Setup Demuxer Codecs
			if (setupDemuxerCodecs(&ds) < 0)
				exit(1);

			pCodecCtxVideo = ds.pCodecCtxVideo;
			pCodecCtxAudio = ds.pCodecCtxAudio;

			// Dump information about next input file
			if (debug > 0)
				dump_format(pFormatCtx, input_file_pos, input_files[input_file_pos], false);

			// Read first packet of new input file
			got_packet = av_read_frame(pFormatCtx, &packet);
		} else if (got_packet < 0 && remux_timestamps)
			last_frame = 1; // Done remuxing/combining files

		// Check if we need to exit
		if (!last_frame 
			&& ((!continueDecoding && !hqvData->do_hqv && !hqvData->do_dqv && !hqvData->do_dvd) 
			|| got_packet < 0)) 
		{
			// Flush Video Decoder
			last_frame = 1;

			// Last frame Dummy frame
			av_init_packet(&packet);
			packet.data = NULL;
			packet.size = 0;
			do_flush = 1;
			if (cs->do_audio)
				packet.stream_index = audioStream;
			else
				packet.stream_index = videoStream;
			av_log(NULL, AV_LOG_VERBOSE, 
				"\rSending Dummy packet to flush stream %d decoder.\n", packet.stream_index);
		} else if (last_frame && !do_flush)
			exit_loop = 1;
		else if (do_flush == 2) {
			// Last frame Dummy frame
			av_init_packet(&packet);
			packet.data = NULL;
			packet.size = 0;
			packet.stream_index = videoStream;
			av_log(NULL, AV_LOG_VERBOSE, 
				"\rSending Dummy packet to flush stream %d decoder.\n", packet.stream_index);
		} else
			hqvData->brfp = url_ftell(pFormatCtx->pb);

		// Last final flush of buffers
		if (exit_loop) {
			// Audio Encoder FIFO Buffer Flush
			if (encode && csa->do_audio)
				audio_bytes += flush_audio_encoder(csa, 
					audio_fifo, audio_outbuf, audio_buf_out, audio_buf_size, aFile, 1);

			// Flush Video Encoder
			if (encode && csv->do_video)
				video_bytes += flush_video_encoder(csv, 
					video_outbuf, video_outbuf_size, vFile);
		}
		
		// Calculate encoder time
		if (encode && (cs->mux || cs->do_demux)) {
			// Get Stream Encoding PTS
			if (cs->do_video) {
				e_vtime = csv->video_st->pts.val;
				e_vtime = (e_vtime/(double)csv->video_st->time_base.den);
			}
			
			if (cs->do_audio) {
				e_atime = csa->audio_st->pts.val;
				e_atime = (e_atime/(double)csa->audio_st->time_base.den);
			}
		}
		
		// Status Output
		if ((do_record || do_record_dvd) && show_status) {
			char output_line[1024];
			double cur_time = ((av_gettime() / 1000000) - starttime);

			sprintf(output_line, "[%"PRId64"] %-3dfps A%#0.2f/%#0.2f V%#0.2f/%#0.2f"
				" F(%"PRId64"/%d+/%d-) S(%3d/%3d)", 
				(av_gettime() / 1000000) - starttime, 
				(vcount<cur_time || cur_time <= 0)?0:(int)((double)vcount/cur_time),
				//encode?e_atime:atime, encode?e_vtime:vtime, 
				atime, e_atime, vtime, e_vtime, 
				o_vcount, frame_dup, frame_drop,
				(int)(drift*1000), (int)drift_offset);

			if (hqvData->do_dqv || hqvData->do_dvd) 
				sprintf(output_line+strlen(output_line), 
					" Q(%d)", hqvData->fifo?av_fifo_size(hqvData->fifo):0);

			if (!exit_loop) {
				fprintf(stderr, "%s   \r", output_line);
				fflush(stderr);
			} else
				fprintf(stderr, "%s   \n", output_line);
		}

		// Break out of loop
		if (exit_loop) {
			if (show_status)
				av_log(NULL, AV_LOG_INFO, "\n");
			break;
		}

		// Video
		if (cs->do_video && packet.stream_index == videoStream) {
			AVPacket avpkt = packet;
			int len = 0;
			int64_t toffset;
			int do_decode = 1;

			// Generate DTS/PTS values
			if (do_ptsgen) {
				if (AVTimestamp.video_dvd_pts == -1) {
					AVTimestamp.video_dvd_pts = 0;
				} else {
					if (packet.dts != AV_NOPTS_VALUE) {
						if (packet.duration > 0) {
							if (!remux_timestamps && packet.dts != AVTimestamp.video_dvd_pts + packet.duration) {
								if (AVTimestamp.ptsgen_vdiff != ((AVTimestamp.video_dvd_pts + packet.duration) - packet.dts))
									av_log(NULL, AV_LOG_VERBOSE, "PTSGEN: Video #%"PRId64" has bad timestamp %"PRId64" instead of %"PRId64" (%"PRId64"/%"PRId64")\n",
										vcount, packet.dts, AVTimestamp.video_dvd_pts + packet.duration, 
										AVTimestamp.ptsgen_vdiff, (AVTimestamp.video_dvd_pts + packet.duration) - packet.dts);
								AVTimestamp.ptsgen_vdiff = (AVTimestamp.video_dvd_pts + packet.duration) - packet.dts;
							} else
								AVTimestamp.ptsgen_vdiff = 0;
							packet.dts = AVTimestamp.video_dvd_pts + packet.duration;
						}

					}
					if (packet.pts != AV_NOPTS_VALUE) {
						if (packet.duration > 0)
							packet.pts = AVTimestamp.video_dvd_pts + packet.duration;
					}
				}

				av_log(NULL, AV_LOG_DEBUG, "\rPTSGEN: Video #%"PRId64" packet.dts=%"PRId64" video_dvd_pts=%"PRId64" packet.duration=%d packet.size=%d\n",
					vcount, packet.dts, AVTimestamp.video_dvd_pts, packet.duration, packet.size);
			} else if (AVTimestamp.video_dvd_pts == -1)
				AVTimestamp.video_dvd_pts = 0;
			
			// Correct bad timestamps from demuxed video input
			if (do_resync && !do_flush && 
					AVTimestamp.video_dvd_pts > 0 && 
					AVTimestamp.video_dvd_pts >= packet.dts) {

				if (!AVTimestamp.video_wrap_ts || (AVTimestamp.video_wrap_ts > packet.dts)) {
					av_log(NULL, AV_LOG_WARNING, 
						"\rWarning: Video DTS is a monotone timestamp count=%d "
						"last=%"PRId64" current=%"PRId64" duration=%d corrected=%"PRId64".\n",
						AVTimestamp.video_wrap, AVTimestamp.video_dvd_pts, packet.dts, 
						packet.duration, AVTimestamp.video_dvd_pts + packet.duration);

					AVTimestamp.video_wrap_start_time = AVTimestamp.video_dvd_pts + packet.duration;

					// Sync up Audio/Video
					if (cs->do_audio && !remux_timestamps && (AVTimestamp.audio_wrap != AVTimestamp.video_wrap)) {
						av_log(NULL, AV_LOG_WARNING, "\rWarning: Video synced up to Audio start time %"PRId64"\n",
							AVTimestamp.audio_wrap_start_time);
						AVTimestamp.video_dvd_pts = AVTimestamp.audio_wrap_start_time;	
					}

					AVTimestamp.video_wrap++;
				}

				AVTimestamp.video_wrap_ts = packet.dts;

				packet.dts = AVTimestamp.video_dvd_pts + packet.duration;
				packet.pts = AVTimestamp.video_dvd_pts + packet.duration;
			}

			if (AVTimestamp.rstart == -1 && packet.dts != AV_NOPTS_VALUE)
				AVTimestamp.rstart = packet.dts;
			if (AVTimestamp.vstart == -1 && packet.dts != AV_NOPTS_VALUE)
				AVTimestamp.vstart = packet.dts;

			toffset = AVTimestamp.rstart;
			if (!do_sync)
				toffset = AVTimestamp.vstart;
				
			if (packet.flags&PKT_FLAG_KEY)
				is_keyframe = 1;
			else
				is_keyframe = 0;

			if (!do_flush) {
				if (packet.dts != AV_NOPTS_VALUE)
					AVTimestamp.vlastdts = (packet.dts - toffset);

				vtime = (double)AVTimestamp.vlastdts / (double)tbn_v;
			}

			// Remux input MPEG2 timestamps
			if (remux_timestamps && packet.size > 0 && packet.data) {
				double cur_time;

				if (!AVTimestamp.video_dvd_pts && !start_time)
					AVTimestamp.video_dvd_pts = packet.dts;

				cur_time = 
					((double)((AVTimestamp.video_dvd_pts+AVTimestamp.dvd_segment_voffset)-AVTimestamp.rstart)/(double)tbn_v);
				if (cur_time < 0)
					cur_time = 0;

				if (cur_time >= start_time && (is_keyframe || video_pts)) {
					int end_check = ss+start_time;

					// End time reached
					if (ss && is_keyframe && cur_time >= end_check)
						break;

					av_log(NULL, AV_LOG_DEBUG, 
						"Video #%"PRId64" pts: %"PRId64" dts: %"PRId64" duration: %d video_pts: %"PRId64" "
						"offset: %"PRId64" (%"PRId64" %"PRId64") start: %d end: %d\n",
						vcount, packet.pts, packet.dts, packet.duration, video_pts, 
						AVTimestamp.dvd_segment_voffset, AVTimestamp.video_dvd_pts, packet.dts, start_time, 
						ss);

					if (rewrite_timestamps) {
						if (AVTimestamp.video_dvd_pts > packet.dts)
							AVTimestamp.dvd_segment_voffset = AVTimestamp.video_dvd_pts;

						AVTimestamp.video_dvd_pts = packet.dts;

						if (video_pts == 0)
							AVTimestamp.video_start_pts = AVTimestamp.video_dvd_pts;

						packet.pts = video_pts+(AVTimestamp.vstart-AVTimestamp.rstart);
						packet.dts = AV_NOPTS_VALUE;

						video_pts += packet.duration;
					} else {
						if (AVTimestamp.video_dvd_pts > packet.dts)
							AVTimestamp.dvd_segment_voffset = AVTimestamp.video_dvd_pts;

						AVTimestamp.video_dvd_pts = packet.dts;

						video_pts = (packet.pts+AVTimestamp.dvd_segment_voffset)-AVTimestamp.rstart;
						packet.pts = video_pts;
						video_pts = (packet.dts+AVTimestamp.dvd_segment_voffset)-AVTimestamp.rstart;
						packet.dts = video_pts;
						video_pts = 1;
					}

					write_dvdmux(pMPEGVideoFormatCtx, &packet, 1/*video*/, 0, 0);
					vcount++;
				} else {
					AVTimestamp.video_dvd_pts = packet.dts;

					av_log(NULL, AV_LOG_DEBUG, 
						"Skipping Video #%"PRId64" pts: %"PRId64" dts: %"PRId64" duration: %d video_pts: %"PRId64" "
						"offset: %"PRId64" (%"PRId64" %"PRId64") start: %d end: %d key: %d\n",
						vcount, packet.pts, packet.dts, packet.duration, video_pts, 
						AVTimestamp.dvd_segment_voffset, AVTimestamp.video_dvd_pts, packet.dts, start_time, 
						ss, is_keyframe);
				}
			}
			if (remux_timestamps)
				do_decode = 0;

			// Mpeg2 DVD Muxer
			if (hqvData->do_dvd && packet.size > 0 && packet.data) {
				// Segmentation
				if (dvd_video_segment_needed && is_keyframe) {
					dvd_video_segment_needed = 0;

					if (do_record_dvd) {
						if (cs->do_audio)
							dvd_audio_segment_needed = 1;

						if (!cs->do_audio && pMPEGVideoFormatCtx) {
							if (show_status)
								av_log(NULL, AV_LOG_INFO, "\n");
							stop_dvdmux(pMPEGVideoFormatCtx);
							pMPEGVideoFormatCtx = NULL;
						}
					}

					// Extended Mode off/on
					if (!hqvData->record) {
						if (do_record_dvd && pMPEGVideoFormatCtx) {
							stop_dvdmux(pMPEGVideoFormatCtx);
							pMPEGAudioFormatCtx = NULL;
							pMPEGVideoFormatCtx = NULL;
						}
						do_record_dvd = 0;
					} else {
						if (!do_record_dvd) {
							if (cs->do_audio)
								dvd_audio_segment_needed = 1;
							do_record_dvd = 1;
						}
					}

					if (do_record_dvd) {
						pMPEGVideoFormatCtx = 
							init_dvdmux(csv, 
								pFormatCtx, videoStream, audioStream, 
								hqvData->output);
						if (hqvData->do_dvd && do_record_dvd && hqvData->archive)
							if (link(hqvData->output, hqvData->output_tmp))
								av_log(NULL, AV_LOG_ERROR, "\rError: link failed for hqv output.\n");

						if (debug > 0)
							dump_format(pMPEGVideoFormatCtx, 
								hqvData->count, hqvData->output, 1);
					}
					if (normalize_timestamps)
						AVTimestamp.dvd_segment_voffset = (packet.dts-toffset);

					AVTimestamp.video_start_pts = packet.dts;
				}

				// Mux Video into DVD MPEG Format	
				if (do_record_dvd) {
					write_dvdmux(pMPEGVideoFormatCtx, 
						&packet, 1/*video*/, toffset+AVTimestamp.dvd_segment_voffset, 0);
					if (!hqvData->do_dqv)
						vcount++;
				}
				AVTimestamp.video_dvd_pts = packet.dts;
				if (is_keyframe)
					video_dvd_keyframe = packet.dts - toffset;
			} else if (!remux_timestamps)
				AVTimestamp.video_dvd_pts = packet.dts;

			if (hqvData->do_dvd && !hqvData->do_dqv)
				do_decode = 0;

			// Only copy video packet into muxer
			if (cs->copy_video && avpkt.size > 0) {
				do_decode = 0;
				vcount++;
        			o_vcount++;
				video_bytes += avpkt.size;
				video_pts++;

				write_dvdmux(csv->oc, &packet, 1/*video*/, toffset, 0);
			}

			// Decode video frame
			frameFinished = 0;
			while ((avpkt.size > 0 || do_flush) && do_decode) {
				avcodec_get_frame_defaults(&pFrame);
				len =
					avcodec_decode_video2(pCodecCtxVideo, &pFrame,
								  &frameFinished, &avpkt);
			
				if (do_flush)
					av_log(NULL, AV_LOG_VERBOSE, 
						"\rVideo decoder flush packet size=%d/%d len=%d\n", 
						packet.size, avpkt.size, len);
				
				// Bad Frame
				if (len < 0) {
					av_log(NULL, AV_LOG_WARNING, 
						"\rVideo decoded bad frame #%"PRId64" at (%0.2f) got %d.\n", 
						vcount, (double)vcount/in_fps, len);

					do_flush = 0;

					break;
				}	

				// Did we get a video frame?
				if (frameFinished && len > 0) {
					double vdelta = 0;

					avpkt.data += len;
					avpkt.size -= len;

					// time in ms that we are behind real time
					vdelta = (packet.dts - toffset) - ((double)o_vcount * v_dur_out);

					vcount++;
				
					// Filter
					if (ist->input_video_filter) {
						ist->st = pFormatCtx->streams[videoStream];
						if (encode)
							ost->st = csv->oc->streams[0];
						ist->pts = av_rescale_q(packet.pts, ist->st->time_base, AV_TIME_BASE_Q);
						av_vsrc_buffer_add_frame(ist->input_video_filter, &pFrame,
							ist->pts, ist->st->codec->sample_aspect_ratio);	
					}

					int frame_available = !ist->out_video_filter || avfilter_poll_frame(ist->out_video_filter->inputs[0]);

					// Filter
					while (frame_available) {
						AVRational ist_pts_tb;
       						if (ist->st->codec->codec_type == AVMEDIA_TYPE_VIDEO && ist->out_video_filter) {
							get_filtered_video_frame(ist->out_video_filter, &picture, &ist->picref, &ist_pts_tb);
							if (encode)
								if (ist->picref)
									ist->pts = ist->picref->pts;
						}
					
        					if (o_vcount == 0 || vdelta >= -v_dur_out) {
        						// Deinterlace
        						if (cs->deinterlace && pFrame.interlaced_frame && !buf_to_free) {
        							buf_to_free = NULL;
        							pre_process_video_frame(pCodecCtxVideo, 
        										(AVPicture *)&pFrame, 
        										&buf_to_free);
        						}
        					
        						o_vcount++;
        						vdelta = 
        							(packet.dts - toffset) - 
        								((double)o_vcount * v_dur_out);
        
        						if (encode) { 
        							// Encode Frame
        							int out_size;
        						
        							picture.pts = video_pts++;
        							out_size = avcodec_encode_video(csv->vEncCtx, 
        								video_outbuf, video_outbuf_size, &picture);
        
        							video_bytes += out_size;
        						
        							if (out_size > 0) {
        								if (cs->mux || cs->do_demux) {
        									mux_frame(csv, video_outbuf, out_size,
        										1/*video*/, 0/*rescale*/, 
        										(cs->do_audio && cs->do_video 
        										&& !cs->do_demux)
        										/*interleave*/);
        								} else if (vFile) {
        									if (fwrite(video_outbuf, 
        										1, out_size, vFile) != out_size)
        											goto failure;
        								}
        							}
        						} else if (vFile) {
        							int ret_bytes;
        
        							if ((ret_bytes = write_yuv_frame(vFile, 
        								&picture, cs->h)) > 0) 
        									video_bytes += ret_bytes;
        							else
        								goto failure;
        						}
        					} else {
        						// Frame Drop
        						frame_drop++;
        					
        						av_log(NULL, AV_LOG_DEBUG, 	
        							"\r[%"PRId64"] (%0.3f) Drop frame %d\n",
        							vcount, vdelta,
        							frame_drop);
        					}
        				
        					// Duplicate Frame
        					if (o_vcount != 0 && vdelta >= v_dur_out) {
        						// Deinterlace
        						if (cs->deinterlace && pFrame.interlaced_frame && !buf_to_free) {
        							buf_to_free = NULL;
        							pre_process_video_frame(pCodecCtxVideo, 
        										(AVPicture *)&pFrame, 
        										&buf_to_free);
        						}
        					
        						frame_dup++;
        						o_vcount++;
        						vdelta = (packet.dts - toffset) - 
        							((double)o_vcount * v_dur_out);
        
        							if (encode) { 
        								// Encode
        								int out_size;
        						
        								picture.pts = video_pts++;
        								out_size = avcodec_encode_video(csv->vEncCtx, 
        									video_outbuf, video_outbuf_size, &picture);
        						
        								video_bytes += out_size;
        
        								if (out_size > 0) {
        									if (cs->mux || cs->do_demux) {
        										mux_frame(csv, video_outbuf, out_size, 
        											/*video*/1, 0/*rescale*/, 
        											(cs->do_audio && 
        											cs->do_video && 
        											!cs->do_demux)/*interleave*/);
        									} else if (vFile)
        										if (fwrite(video_outbuf, 
        											1, out_size, vFile) != out_size)
        												goto failure;
        								}
        							} else if (vFile) {
        								int ret_bytes;
        
        								if ((ret_bytes = 
        									write_yuv_frame(vFile, &picture, cs->h)) > 0)
        									video_bytes += ret_bytes;
        								else
        									goto failure;
        							}
        					
        							av_log(NULL, AV_LOG_DEBUG, 	
        								"\r[%"PRId64"] (%0.3f) Duplicate frame %d\n",
        								vcount, vdelta,
        								frame_dup);
        
        					}

						// Filter
						frame_available = ist->out_video_filter && avfilter_poll_frame(ist->out_video_filter->inputs[0]);
       						if(ist->picref)
               						avfilter_unref_buffer(ist->picref);
       					}
				
					if (cs->deinterlace && pFrame.interlaced_frame && buf_to_free) {
						av_free(buf_to_free);
						buf_to_free = NULL;
					}
				
					drift_offset = (vdelta / 100);
				} else
					do_flush = 0;

				// Only 1 frame per video packet
				// FFmpeg changed this around Nov. 2009
				// No longer can break out like this and work
				//break;
			}
		// Audio
		} else if (cs->do_audio
				   && packet.stream_index == audioStream) {
			int64_t toffset;
			int do_decode = 1;
			AVPacket avpkt = packet;

			// Generate DTS/PTS values
			if (do_ptsgen) {
				if (AVTimestamp.audio_dvd_pts == -1) {
					AVTimestamp.audio_dvd_pts = 0;
				} else {
					if (packet.dts != AV_NOPTS_VALUE) {
						if (packet.duration > 0) {
							if (!remux_timestamps && packet.dts != AVTimestamp.audio_dvd_pts + packet.duration) {
								if (AVTimestamp.ptsgen_adiff != ((AVTimestamp.audio_dvd_pts + packet.duration) - packet.dts))
									av_log(NULL, AV_LOG_VERBOSE, "PTSGEN: Audio #%"PRId64" has bad timestamp %"PRId64" instead of %"PRId64" (%"PRId64"/%"PRId64")\n",
										acount, packet.dts, AVTimestamp.audio_dvd_pts + packet.duration, 
										AVTimestamp.ptsgen_adiff, (AVTimestamp.audio_dvd_pts + packet.duration) - packet.dts);
								AVTimestamp.ptsgen_adiff = (AVTimestamp.audio_dvd_pts + packet.duration) - packet.dts;
							} else
								AVTimestamp.ptsgen_adiff = 0;
							packet.dts = AVTimestamp.audio_dvd_pts + packet.duration;
						}
					}
					if (packet.pts != AV_NOPTS_VALUE) {
						if (packet.duration > 0)
							packet.pts = AVTimestamp.audio_dvd_pts + packet.duration;
					}
				}

				av_log(NULL, AV_LOG_DEBUG, "\rPTSGEN: Audio #%"PRId64" packet.dts=%"PRId64" audio_dvd_pts=%"PRId64" packet.duration=%d packet.size=%d\n",
					acount, packet.dts, AVTimestamp.audio_dvd_pts, packet.duration, packet.size);
			} else if (AVTimestamp.audio_dvd_pts == -1)
				AVTimestamp.audio_dvd_pts = 0;
			
			// Bad audio frame offset
			if (packet.size > pCodecCtxAudio->frame_size && pCodecCtxAudio->frame_size > 1 && (packet.size % pCodecCtxAudio->frame_size)) {
				char abuf[pCodecCtxAudio->frame_size];

				av_log(NULL, AV_LOG_WARNING, 
					"\rAdjusting oversized Audio frame #%"PRId64" at (%0.2f) size %d pos %"PRId64"/%"PRId64" duration %d bytes %"PRId64"\n", 
					acount, (double)audio_pts/(double)pCodecCtxAudio->sample_rate, 
					packet.size, hqvData->brfp, packet.pos, packet.duration, audio_pts);

				memcpy(abuf, packet.data+(packet.size-pCodecCtxAudio->frame_size), pCodecCtxAudio->frame_size);
				memcpy(packet.data, abuf, pCodecCtxAudio->frame_size);
				packet.size = pCodecCtxAudio->frame_size;
				avpkt.size = pCodecCtxAudio->frame_size;
			} else if (packet.size && pCodecCtxAudio->frame_size > 1 && packet.size < pCodecCtxAudio->frame_size) {
				av_log(NULL, AV_LOG_WARNING, 
					"\rundersized Audio frame #%"PRId64" at (%0.2f) size %d pos %"PRId64"/%"PRId64" duration %d bytes %"PRId64"\n", 
					acount, (double)audio_pts/(double)pCodecCtxAudio->sample_rate, 
					packet.size, hqvData->brfp, packet.pos, packet.duration, audio_pts);
			}

			if (AVTimestamp.audio_dvd_pts > 0 && packet.dts != AV_NOPTS_VALUE && AVTimestamp.audio_dvd_pts == packet.dts) {
				// Duplicate PTS/DTS
				av_log(NULL, AV_LOG_WARNING, 
					"\rWarning: Audio frame #%"PRId64" has duplicate pts/dts, incrementing from [%"PRId64"/%"PRId64"] to [%"PRId64"/%"PRId64"] \n", 
					acount, packet.pts, packet.dts, (packet.pts+packet.duration), (packet.dts+packet.duration));
				packet.dts += packet.duration;
				packet.pts += packet.duration;
			} else if (do_resync && !do_flush && 
					AVTimestamp.audio_dvd_pts > 0 && 
					AVTimestamp.audio_dvd_pts >= packet.dts) {

				// Correct bad timestamps from demuxed audio input
				if (!AVTimestamp.audio_wrap_ts || (AVTimestamp.audio_wrap_ts > packet.dts)) {
					av_log(NULL, AV_LOG_WARNING, 
						"\rWarning: Audio DTS is a monotone timestamp count=%d "
						"last=%"PRId64" current=%"PRId64" duration=%d corrected=%"PRId64".\n",
						AVTimestamp.audio_wrap, AVTimestamp.audio_dvd_pts, packet.dts, packet.duration, 
						AVTimestamp.audio_dvd_pts + packet.duration);

					AVTimestamp.audio_wrap_start_time = AVTimestamp.audio_dvd_pts + packet.duration;

					// Sync up Audio/Video
					if (cs->do_video && !remux_timestamps && (AVTimestamp.audio_wrap != AVTimestamp.video_wrap)) {
						av_log(NULL, AV_LOG_WARNING, "\rWarning: Audio synced up to Video start time %"PRId64"\n",
							AVTimestamp.video_wrap_start_time);
						AVTimestamp.audio_dvd_pts = AVTimestamp.video_wrap_start_time;	
					}

					AVTimestamp.audio_wrap++;
				}

				AVTimestamp.audio_wrap_ts = packet.dts;

				packet.dts = AVTimestamp.audio_dvd_pts + packet.duration;
				packet.pts = AVTimestamp.audio_dvd_pts + packet.duration;
			}

			if (AVTimestamp.rstart == -1 && packet.dts != AV_NOPTS_VALUE)
				AVTimestamp.rstart = packet.dts;
			if (AVTimestamp.astart == -1 && packet.dts != AV_NOPTS_VALUE)
				AVTimestamp.astart = packet.dts;
					
			toffset = AVTimestamp.rstart;
			if (!do_sync)
				toffset = AVTimestamp.astart;

			if (packet.dts != AV_NOPTS_VALUE && !remux_timestamps)
				AVTimestamp.audio_dvd_pts = packet.dts;

			if (packet.size > 0 && !remux_timestamps)
				audio_pts += packet.size;

			// PTS/DTS
			if (!do_flush) {
				if (packet.dts != AV_NOPTS_VALUE)
					AVTimestamp.alastdts = packet.dts - toffset;
				atime = (double)AVTimestamp.alastdts / (double)tbn_a;
			}	

			// Remux input MPEG2 timestamps
			if (remux_timestamps && packet.size > 0 && packet.data) {
				double cur_time;

				if (!AVTimestamp.audio_dvd_pts && !start_time)
					AVTimestamp.audio_dvd_pts = packet.dts;

				cur_time = ((double)((AVTimestamp.audio_dvd_pts+AVTimestamp.dvd_segment_aoffset)-AVTimestamp.rstart)/(double)tbn_a);
				if (cur_time < 0)
					cur_time = 0;

				if ((cur_time >= start_time) 
					&& (video_pts || !start_time) 
					&& (audio_pts > 0 || !rewrite_timestamps || !start_time || (packet.dts >= (AVTimestamp.video_start_pts-packet.size)))) 
				{ 
					av_log(NULL, AV_LOG_DEBUG, 
						"Audio #%"PRId64" pts: %"PRId64" dts: %"PRId64" duration: %d audio_pts: %"PRId64" offset: %"PRId64" (%"PRId64" %"PRId64") start: %d end: %d\n",
						acount, 
						packet.pts, packet.dts, 
						packet.duration, audio_pts, 
						AVTimestamp.dvd_segment_aoffset, AVTimestamp.audio_dvd_pts, packet.dts, 
						start_time, ss);

					if (rewrite_timestamps) {
						if (AVTimestamp.audio_dvd_pts > packet.dts)
							AVTimestamp.dvd_segment_aoffset = AVTimestamp.audio_dvd_pts;

						AVTimestamp.audio_dvd_pts = packet.dts;

						packet.pts = audio_pts+(AVTimestamp.astart-AVTimestamp.rstart);
						packet.dts = AV_NOPTS_VALUE;
						audio_pts += packet.duration;
					} else {
						if (AVTimestamp.audio_dvd_pts > packet.dts)
							AVTimestamp.dvd_segment_aoffset = AVTimestamp.audio_dvd_pts;

						AVTimestamp.audio_dvd_pts = packet.dts;

						audio_pts = (packet.pts+AVTimestamp.dvd_segment_aoffset)-AVTimestamp.rstart;
						packet.pts = audio_pts;
						audio_pts = (packet.dts+AVTimestamp.dvd_segment_aoffset)-AVTimestamp.rstart;
						packet.dts = audio_pts;
						audio_pts = 1;
					}

					write_dvdmux(pMPEGAudioFormatCtx, &packet, 0/*audio*/, 0, 0);
					acount++;
				} else {
					AVTimestamp.audio_dvd_pts = packet.dts;

					av_log(NULL, AV_LOG_DEBUG, 
						"Skipping Audio #%"PRId64" pts: %"PRId64" dts: %"PRId64" duration: %d audio_pts: %"PRId64" offset: %"PRId64" (%"PRId64" %"PRId64") start: %d end: %d\n",
						acount, packet.pts, packet.dts, packet.duration, audio_pts, 
						AVTimestamp.dvd_segment_aoffset, AVTimestamp.audio_dvd_pts, packet.dts, start_time, ss);
				}
			}
			if (remux_timestamps)
				do_decode = 0;

			// Mpeg2 DVD Muxer
			if (hqvData->do_dvd && packet.size > 0 && packet.data) {
				// Segmentation
				if (dvd_audio_segment_needed && (packet.dts >= (AVTimestamp.video_start_pts-packet.size))) {
					dvd_audio_segment_needed = 0;
					if (do_record_dvd || hqvData->record) {
						if (do_record_dvd && pMPEGAudioFormatCtx) {
							if (show_status)
								av_log(NULL, AV_LOG_INFO, "\n");
							stop_dvdmux(pMPEGAudioFormatCtx);
							pMPEGAudioFormatCtx = NULL;
						}

						// Extended mode turn off
						if (!cs->do_video) {
							if (!hqvData->record)
								do_record_dvd = 0;
							else
								do_record_dvd = 1;
						}

						if (do_record_dvd) {
							if (cs->do_video) 
								pMPEGAudioFormatCtx = pMPEGVideoFormatCtx;
							else {
								pMPEGAudioFormatCtx = init_dvdmux(csa, pFormatCtx, videoStream, audioStream, hqvData->output);
								if (hqvData->do_dvd && do_record_dvd && hqvData->archive)
									if (link(hqvData->output, hqvData->output_tmp))
										av_log(NULL, AV_LOG_ERROR, "\rError: link failed for hqv output.\n");

								if (debug > 0)
									dump_format(pMPEGAudioFormatCtx, hqvData->count, hqvData->output, 1);
							}
						}
					}
					if (normalize_timestamps)
						AVTimestamp.dvd_segment_aoffset = (packet.dts-toffset);
				}

				// Mux Audio into DVD MPEG format
				if (do_record_dvd) {
					write_dvdmux(pMPEGAudioFormatCtx, &packet, 0/*audio*/, toffset+AVTimestamp.dvd_segment_aoffset, 0);
					if (!hqvData->do_dqv)
						acount++;
				}
			}
			if (hqvData->do_dvd && !hqvData->do_dqv)
				do_decode = 0;

			// Only copy audio packet into muxer
			if (cs->copy_audio && avpkt.size > 0) {
				do_decode = 0;
				acount++;
				audio_bytes += avpkt.size;

				write_dvdmux(csa->oc, &packet, 0/*audio*/, toffset, 0);
			}

			while ((avpkt.size > 0 || do_flush) && do_decode) {
				// Decode audio frame
				int data_size = audio_buf_size;
				int len;

				len = avcodec_decode_audio3(pCodecCtxAudio,
								  (short *) audio_buf_out,
								  &data_size, &avpkt);

				if (do_flush)
					av_log(NULL, AV_LOG_VERBOSE, "\rAudio packet size=%d/%d data size=%d buffer size=%d len=%d\n", 
						packet.size, avpkt.size, data_size, audio_buf_size, len);

				// Bad Frame
				if (len < 0) {
					av_log(NULL, AV_LOG_WARNING, "\rBad audio frame #%"PRId64" at (%0.2f) size %d pos %"PRId64"/%"PRId64" duration %d returned %d\n", 
							acount, (double)audio_pts/(double)pCodecCtxAudio->sample_rate, packet.size, hqvData->brfp, packet.pos, packet.duration, len);

					if (do_flush) {
						if (!cs->do_video)
							do_flush = 0;
						else
							do_flush = 2;
					}

					break;
				}	
				if (debug > 4 && packet.size == avpkt.size)
					av_log(NULL, AV_LOG_DEBUG, 
						"\rAudio frame #%"PRId64" at (%0.2f) size %d pos %"PRId64"/%"PRId64" pts/dts %"PRId64"/%"PRId64" duration %d returned %d\n", 
						acount, (double)audio_pts/(double)pCodecCtxAudio->sample_rate, 
						packet.size, hqvData->brfp, packet.pos, packet.pts, packet.dts, packet.duration, len);

				avpkt.data += len;
				avpkt.size -= len;

				// No audio frame
				if (data_size <= 0) {
					if (do_flush && avpkt.size <= 0) {
						if (!cs->do_video)
							do_flush = 0;
						else
							do_flush = 2;
						break;
					}

					continue;
				} else {	// Got audio frame      
					acount++;
					
					// Resample
					if (in_achan != cs->achan
				    		|| in_arate != cs->arate) {
						data_size =
							resample_audio_frame
							(av_convert_ctx, in_achan,
					 		in_afmt, (short *) audio_buf_out, data_size,
							out_afmt, cs->achan, (short *)audio_buf_out_rs);
						
						av_fifo_generic_write(audio_fifo, (uint8_t *)audio_buf_out_rs, data_size, NULL );
					} else
						av_fifo_generic_write(audio_fifo, (uint8_t *)audio_buf_out, data_size, NULL );

					// Encode
					if (encode) { 
						int osize = av_get_bits_per_sample_fmt(csa->aEncCtx->sample_fmt)/8;
						int frame_bytes;
						int size_out = data_size;
						
						if (csa->aEncCtx->frame_size > 1)
							frame_bytes = csa->aEncCtx->frame_size * osize * csa->achan;
						else {
							// Raw Audio Out
							int coded_bps = av_get_bits_per_sample(csa->aEncCtx->codec->id)/8;
							size_out = size_out * csa->aEncCtx->channels * osize;
							size_out /= osize;
							if (coded_bps)
								size_out *= coded_bps;
							frame_bytes = size_out;
						}

						while( av_fifo_size(audio_fifo) >= frame_bytes ) {
							int out_size = 0;

							av_fifo_generic_read( audio_fifo, audio_buf_out, frame_bytes, NULL );
							out_size = avcodec_encode_audio(csa->aEncCtx, 
								audio_outbuf, frame_bytes, (short *)audio_buf_out);

							if (out_size > 0) {
								audio_bytes += out_size;

								// Write to file
								if (cs->mux || cs->do_demux) {
									mux_frame(csa, audio_outbuf, out_size, 
										/*audio*/0, 0/*rescale*/, 
										(cs->do_audio && cs->do_video && !cs->do_demux)/*interleave*/);
								} else if (aFile)
									if (fwrite(audio_outbuf, 1,
									   out_size, aFile) != out_size)
									   	goto failure;
							}
						}
					} else if (aFile) {
						audio_bytes += data_size;

						// Write to file
						av_fifo_generic_read( audio_fifo, audio_buf_out, data_size, NULL );
						if (fwrite(audio_buf_out, 1,
						   	data_size, aFile) != data_size)
								goto failure;
					}
				
				}
			}
		} else if (cs->do_sub
				   && packet.stream_index == subStream) {
			int len;
			AVPacket avpkt = packet;

			// PTS/DTS
			if (!do_flush) {
				if (packet.dts != AV_NOPTS_VALUE)
					AVTimestamp.slastdts = packet.dts - AVTimestamp.rstart;
			}	

			if (pCodecCtxSub) {
				while (avpkt.size > 0) {
					len = avcodec_decode_subtitle2(pCodecCtxSub, 
						&subtitle, &got_subtitle, &avpkt);

					if (len < 0) {
						av_log(NULL, AV_LOG_ERROR, "Error decoding subtitle #%"PRId64"\n", scount);
						break;
					}

					if (got_subtitle) {
						scount++;
						subtitle_to_free = &subtitle;
						avpkt.size = 0;

						av_log(NULL, AV_LOG_DEBUG, 
							"\rSubtitle frame #%"PRId64" size %d pos %"PRId64"/%"PRId64" duration %d returned %d\n", 
								scount, packet.size, hqvData->brfp, packet.pos, packet.duration, len);
					}
				}

				// Free subtitle
				if (subtitle_to_free) {
					if (subtitle_to_free->rects != NULL) {
						for (i = 0; i < subtitle_to_free->num_rects; i++) {
							av_freep(&subtitle_to_free->rects[i]->pict.data[0]);
							av_freep(&subtitle_to_free->rects[i]->pict.data[1]);
							av_freep(&subtitle_to_free->rects[i]);
						}
						av_freep(&subtitle_to_free->rects);
					}
				}
				subtitle_to_free->num_rects = 0;
				subtitle_to_free = NULL;
			} else {
				// Write out subtitle to file or directly mux packet
				scount++;

				av_log(NULL, AV_LOG_DEBUG, 
					"\rSubtitle frame #%"PRId64" size %d pos %"PRId64"/%"PRId64" pts/dts %"PRId64"/%"PRId64" duration %d\n", 
						scount, packet.size, hqvData->brfp, packet.pos, packet.pts, packet.dts, packet.duration);

				//if (cs->mux || cs->do_demux) {
					// Mux packet here
				/*} else*/ if (sFile)
					if (fwrite(packet.data, 1, packet.size, sFile) != packet.size)
						goto failure;
			}

			// Free the packet that was allocated by av_read_frame
			av_free_packet(&packet);
			continue;
		} else {
			// Not a Audio/Video Stream
			av_log(NULL, AV_LOG_DEBUG, 	
					"\rNo Audio/Video data in packet stream %d.\n", 
					packet.stream_index);
			
			// Free the packet that was allocated by av_read_frame
			av_free_packet(&packet);
			continue;
		}
		
		// Free the packet that was allocated by av_read_frame
		av_free_packet(&packet);
		
		// Calculate drift
		if (cs->do_audio && cs->do_video)
			drift = (((double)AVTimestamp.vlastdts / (double)tbn_v) -
			 		((double)AVTimestamp.alastdts / (double)tbn_a));

		//***********************************//
		// File Segment timing or stop time //
		//*********************************// 
		if ((ss > 0 || hqvData->seconds > 0) && !remux_timestamps) {
			// Completely Done, program exit
			if (ss && (vtime == -1 || vtime >= ss) && (atime == -1 || atime >= ss))
				continueDecoding = 0;
			
			// Video Segment Close/Open New
			if (hqvData->seconds) {
				int video_segment = 0;
				int audio_segment = 0;

				if (hqvData->do_dqv || hqvData->do_dvd) {
					double vt = (double)AVTimestamp.vlastdts + (frame_dup * v_dur_in);
					double at = (double)AVTimestamp.alastdts;
					int video_ahead = (vt >= at)?1:0;

					if (hqvData->next_file) {
						// Signal from HQV to segment
						if (vtime != -1 && video_ahead) {
							video_segment = 1;
							segmenttime = vt;
						} else if (atime != -1 ) {
							audio_segment = 1;
							segmenttime = at;
						}
						hqvData->next_file = 0;

						// DVD MPEG2 Remuxing	
						if (hqvData->do_dvd) {
							if (cs->do_video)
								dvd_video_segment_needed = 1;
							else
								dvd_audio_segment_needed = 1;
						}
					} 
					if (atime == -1 || vtime == -1)
						segmenttime = 0;
					if (segmenttime) {
						if (!video_ahead) {
							if (vt >= segmenttime) {
								video_segment = 1;
								segmenttime = 0;
							}
						} else {
							if (at >= segmenttime) {
								audio_segment = 1;
								segmenttime = 0;
							}
						}
					}
				} else {
					if (vtime != -1 && vtime >= (hqvData->seconds*video_segment_count)) {
						if (!segmenttime) {
							segmenttime = (double)AVTimestamp.vlastdts;
							video_segment = 1;
						} else if (AVTimestamp.vlastdts >= segmenttime) {
							segmenttime = 0;
							video_segment = 1;
						}
					}
					if (atime != -1 && atime >= (hqvData->seconds*audio_segment_count)) {
						if (!segmenttime) {
							segmenttime = (double)AVTimestamp.alastdts;
							audio_segment = 1;
						} else if (AVTimestamp.alastdts >= segmenttime) {
							segmenttime = 0;
							audio_segment = 1;
						}
					}
					if (atime == -1 || vtime == -1)
						segmenttime = 0;
				}

				// Video Segment
				if (cs->do_video && video_segment && ((hqvData->do_dqv && hqvData->do_dvd) || !hqvData->do_dvd)) {
					video_segment_count++;
					
					// Close Video Out File
					if (!cs->mux && !cs->do_demux) {
						if (encode && !cs->copy_video) {
							// Flush/close and re-open Video Encoder
							video_bytes += flush_video_encoder(csv, 
												video_outbuf, video_outbuf_size, vFile);
							
							// Close/Reopen Codec
							avcodec_close(csv->vEncCtx);
							if (avcodec_open(csv->vEncCtx, csv->vEncCodec) < 0) {
								av_log(NULL, AV_LOG_FATAL, "\rError: Codec for video encoder failed to open.\n");
								exit(1);	// Codec didn't open
							}
						}
						
						// Close output file
						if (vFile)
							fclose(vFile);
						vFile = NULL;
						
						// Mux Thread
						if (mux_in_thread) {
							// Video stream 
							if (rawyuv)
								sprintf(muxData->video, "%s", rawyuv);
							
							if (rawyuv && (!cs->do_audio || video_segment_count == audio_segment_count)) {
								// Create Mux output file name
								o_filename = OutputFile;
								if (hqvData->do_dqv) {
									dqv_get(OutputFile, cs->hostname, hqvData->channel, hqvData->device, mext, hqvData->round, DQV_DIR);
									av_log(NULL, AV_LOG_INFO, "\rMUX Filename: %s\n", OutputFile);
								} else
									sprintf(OutputFile, "%s-%04d.%s", 
											mout_filename_base, video_segment_count-1, mext);
								
								// Combined Mux file name
								sprintf(muxData->muxfile, "%s", o_filename);
								
								// Signal Muxer to do it
								muxData->muxReady = 1;
							}
						}
						
						// Get new Video Out File Name
						if (hqvData->record) {
							do_record = 1;
							if (!rawyuv)
								rawyuv = VideoFile;
							if (hqvData->do_dqv) {
								dqv_get(VideoFile, cs->hostname, hqvData->channel, hqvData->device, vext, hqvData->round, DQV_DIR);
								av_log(NULL, AV_LOG_INFO, "\rFilename: %s\n", VideoFile);
							} else
								sprintf(VideoFile, "%s-%04d.%s", vout_filename_base, video_segment_count, vext);
						} else {
							rawyuv = NULL;
							do_record = 0;
						}
						
						// ReOpen New Video file
						if (rawyuv) {
							vFile = fopen(rawyuv, "wb");
							if (vFile == NULL) {
								av_log(NULL, AV_LOG_FATAL, 
									"\rError: Unable to open video output file.\n");
								exit(1);
							}
						}
					} else if (cs->do_demux) {
						// Separate muxed format files
						//
						// Flush/close and re-open Video Encoder
						video_bytes += flush_video_encoder(csv, 
							video_outbuf, video_outbuf_size, vFile);
						
						// Get Stream PTS Values
						e_vtime = csv->video_st->pts.val;
						e_vtime = (e_vtime/(double)csv->video_st->time_base.den);
							
						// Stop Muxer and Close File
						if (show_status)
							av_log(NULL, AV_LOG_INFO, "\n");
						stop_muxer(csv);
						video_pts = 0;
						video_bytes = 0;
							
						// Mux Thread
						if (mux_in_thread) {
							// Video stream 
							if (rawyuv)
								sprintf(muxData->video, "%s", rawyuv);
							
							if (rawyuv && (video_segment_count == audio_segment_count)) {
								// Create Mux output file name
								o_filename = OutputFile;
								if (hqvData->do_dqv) {
									dqv_get(OutputFile, cs->hostname, hqvData->channel, hqvData->device, mext, hqvData->round, DQV_DIR);
									av_log(NULL, AV_LOG_INFO, "\rMUX Filename: %s\n", OutputFile);
								} else
									sprintf(OutputFile, "%s-%04d.%s", 
											mout_filename_base, video_segment_count-1, mext);
								
								// Combined Mux file name
								if (o_filename) {
									sprintf(muxData->muxfile, "%s", o_filename);
								
									// Signal Muxer to do it
									muxData->muxReady = 1;
								}
							}
						}
						
						// Get new filename
						if (hqvData->record) {
							do_record = 1;
							if (!rawyuv)
								rawyuv = VideoFile;
							if (hqvData->do_dqv) {
								dqv_get(VideoFile, cs->hostname, hqvData->channel, hqvData->device, vext, hqvData->round, DQV_DIR);
								av_log(NULL, AV_LOG_INFO, "\rVideo Filename: %s\n", VideoFile);
							} else
								sprintf(VideoFile, "%s-%04d.%s", 
									vout_filename_base, video_segment_count, vext);
						} else {
							rawyuv = NULL;
							do_record = 0;
						}
							
						// Reinit Muxer and Encoders
						if(init_muxer(csv, rawyuv))
							goto failure;
						if(cs->do_video && (init_video_encoder(csv) || open_video_encoder(csv)))
							goto failure;
						
						// Start Muxer
						if(start_muxer(csv, rawyuv))
							goto failure;

						if (debug > 0)
							dump_format(csv->oc, video_segment_count, rawyuv, 1);
					} else {
						// Demuxer close/reopen new file
						if (!cs->do_audio || video_segment_count != audio_segment_count) {
							// Get new filename
							if (hqvData->record) {
								do_record = 1;
								if (!o_filename)
									o_filename = OutputFile;
								if (hqvData->do_dqv) {
									dqv_get(OutputFile, cs->hostname, hqvData->channel, hqvData->device, mext, hqvData->round, DQV_DIR);
									av_log(NULL, AV_LOG_INFO, "\rMUX Filename: %s\n", OutputFile);
								} else
									sprintf(OutputFile, "%s-%04d.%s", 
										mout_filename_base, video_segment_count, mext);
							} else {
								o_filename = NULL;
								do_record = 0;
							}
							
							// Flush/close and re-open Video Encoder
							if (cs->do_video)
								video_bytes += flush_video_encoder(csv, 
									video_outbuf, video_outbuf_size, vFile);
							
							// Get Stream PTS Values
							e_vtime = csv->video_st->pts.val;
							e_vtime = (e_vtime/(double)csv->video_st->time_base.den);
							
							if (cs->do_audio) {
								e_atime = csv->audio_st->pts.val;
								e_atime = (e_atime/(double)csv->audio_st->time_base.den);
							}
							
							// Stop Muxer and Close File
							if (!cs->do_audio) {
								if (show_status)
									av_log(NULL, AV_LOG_INFO, "\n");
								stop_muxer(csv);
							} else {
								csv_count++;
								csv = &streams[csv_count%2];
							}
							video_pts = 0;
							video_bytes = 0;
							
							// Reinit Muxer and Encoders
							if(init_muxer(csv, o_filename))
								goto failure;
							if(cs->do_video && (init_video_encoder(csv) || open_video_encoder(csv)))
								goto failure;
							if(cs->do_audio && (init_audio_encoder(csv) || open_audio_encoder(csv)))
								goto failure;
							
							// Start Muxer
							if(start_muxer(csv, o_filename))
								goto failure;

							if (debug > 0)
								dump_format(csv->oc, video_segment_count, o_filename, 1);
						} else {
							// Flush/close and re-open Video Encoder
							if (cs->do_video)
								video_bytes += flush_video_encoder(csv, 
									video_outbuf, video_outbuf_size, vFile);
							
							// Get Stream PTS Values
							e_vtime = csv->video_st->pts.val;
							e_vtime = (e_vtime/(double)csv->video_st->time_base.den);
							
							if (cs->do_audio) {
								e_atime = csv->audio_st->pts.val;
								e_atime = (e_atime/(double)csv->audio_st->time_base.den);
							}
							
							// Stop Muxer
							if (show_status)
								av_log(NULL, AV_LOG_INFO, "\n");
							stop_muxer(csv);

							csv_count++;
							csv = &streams[csv_count%2];
							cs = csv;
							video_pts = 0;
							video_bytes = 0;
						}
					}
					if (do_record)
						av_log(NULL, AV_LOG_WARNING, "%s[%0.2f/%0.2f] [%0.2f] Video Segment %d [%"PRId64"] %d seconds file %s\n",
								slog, e_atime, e_vtime,
								vtime, video_segment_count, AVTimestamp.vlastdts, hqvData->seconds, csv->mux?o_filename:rawyuv);
				}
				
				// Audio Segment
				if (cs->do_audio && audio_segment && ((hqvData->do_dqv && hqvData->do_dvd) || !hqvData->do_dvd)) {
					audio_segment_count++;
					
					// Close Audio Out File
					if (!cs->mux && !cs->do_demux) {
						if (encode && !cs->copy_audio) {
							// Flush Audio Encoder/Fifo
							audio_bytes += flush_audio_encoder(csa,
								audio_fifo, audio_outbuf, audio_buf_out, audio_buf_size, aFile, 0);
							
							// Close/Reopen Codec
							avcodec_close(csa->aEncCtx);
							if (avcodec_open(csa->aEncCtx, csa->aEncCodec) < 0) {
								av_log(NULL, AV_LOG_FATAL, "\rError: Codec for audio encoder failed to open.\n");
								exit(1);	// Codec didn't open
							}
						}
						
						// Close output file
						if (aFile)
							fclose(aFile);
						aFile = NULL;
						
						// Mux Thread
						if (mux_in_thread) {
							// Video stream 
							if (rawpcm)
								sprintf(muxData->audio, "%s", rawpcm);
							
							if (rawpcm && (!cs->do_video || video_segment_count == audio_segment_count)) {
								// Create Mux output file name
								o_filename = OutputFile;
								if (hqvData->do_dqv) {
									dqv_get(OutputFile, cs->hostname, hqvData->channel, hqvData->device, mext, hqvData->round, DQV_DIR);
									av_log(NULL, AV_LOG_INFO, "\rMUX Filename: %s\n", OutputFile);
								} else
									sprintf(OutputFile, "%s-%04d.%s", 
											mout_filename_base, audio_segment_count-1, mext);
								
								// Combined Mux file name
								if (o_filename) {
									sprintf(muxData->muxfile, "%s", o_filename);
								
									// Signal Muxer to do it
									muxData->muxReady = 1;
								}
							}
						}
						
						// Get new Audio Out File Name
						if (hqvData->record) {
							do_record = 1;
							if (!rawpcm)
								rawpcm = AudioFile;
							if (hqvData->do_dqv) {
								dqv_get(AudioFile, cs->hostname, hqvData->channel, hqvData->device, aext, hqvData->round, DQV_DIR);
								av_log(NULL, AV_LOG_INFO, "\rFilename: %s\n", AudioFile);
							} else
								sprintf(AudioFile, "%s-%04d.%s", aout_filename_base, audio_segment_count, aext);
						} else {
							rawpcm = NULL;
							do_record = 0;
						}
						
						// ReOpen Audio file
						if (rawpcm) {
							aFile = fopen(rawpcm, "wb");
							if (aFile == NULL) {
								av_log(NULL, AV_LOG_FATAL, 
									"\rError: Unable to open audio output file.\n");
								exit(1);
							}
						}
					} else if (cs->do_demux) {
						// Separate muxed format files
						//
						// Flush Audio Encoder/Fifo
						audio_bytes += flush_audio_encoder(csa, 
							audio_fifo, audio_outbuf, audio_buf_out, audio_buf_size, aFile, 0);
							
						e_atime = csa->audio_st->pts.val;
						e_atime = (e_atime/(double)csa->audio_st->time_base.den);
							
						// Stop Muxer
						if (show_status)
							av_log(NULL, AV_LOG_INFO, "\n");
						stop_muxer(csa);
						audio_pts = 0;
						audio_bytes = 0;
							
						// Mux Thread
						if (mux_in_thread) {
							// Video stream 
							if (rawpcm)
								sprintf(muxData->audio, "%s", rawpcm);
							
							if (rawpcm && (video_segment_count == audio_segment_count)) {
								// Create Mux output file name
								o_filename = OutputFile;
								if (hqvData->do_dqv) {
									dqv_get(OutputFile, cs->hostname, hqvData->channel, hqvData->device, mext, hqvData->round, DQV_DIR);
									av_log(NULL, AV_LOG_INFO, "\rMUX Filename: %s\n", OutputFile);
								} else
									sprintf(OutputFile, "%s-%04d.%s", 
											mout_filename_base, audio_segment_count-1, mext);
								
								// Combined Mux file name
								sprintf(muxData->muxfile, "%s", o_filename);
							
								// Signal Muxer to do it
								muxData->muxReady = 1;
							}
						}
						
						// Get new filename
						if (hqvData->record) {
							do_record = 1;
							if (!rawpcm)
								rawpcm = AudioFile;
							if (hqvData->do_dqv) {
								dqv_get(AudioFile, cs->hostname, hqvData->channel, hqvData->device, aext, hqvData->round, DQV_DIR);
								av_log(NULL, AV_LOG_INFO, "\rAudio Filename: %s\n", AudioFile);
							} else
								sprintf(AudioFile, "%s-%04d.%s", 	
									aout_filename_base, audio_segment_count, aext);
						} else {
							rawpcm = NULL;
							do_record = 0;
						}
							
						// Reinit Muxer and Encoders
						if(init_muxer(csa, rawpcm))
							goto failure;
						if(cs->do_audio && (init_audio_encoder(csa) || open_audio_encoder(csa)))
							goto failure;
							
						// Start Muxer
						if(start_muxer(csa, rawpcm))
							goto failure;

						if (debug > 0)
							dump_format(csa->oc, audio_segment_count, rawpcm, 1);
					} else {
						// Demuxer close/reopen new file
						if (!cs->do_video || video_segment_count != audio_segment_count) {
							// Get new filename
							if (hqvData->record) {
								do_record = 1;
								if (!o_filename)
									o_filename = OutputFile;
								if (hqvData->do_dqv) {
									dqv_get(OutputFile, cs->hostname, hqvData->channel, hqvData->device, mext, hqvData->round, DQV_DIR);
									av_log(NULL, AV_LOG_INFO, "\rMUX Filename: %s\n", OutputFile);
								} else
									sprintf(OutputFile, "%s-%04d.%s", 	
										mout_filename_base, audio_segment_count, mext);
							} else {
								o_filename = NULL;
								do_record = 0;
							}
							
							// Flush Audio Encoder/Fifo
							audio_bytes += flush_audio_encoder(csa, 
								audio_fifo, audio_outbuf, audio_buf_out, audio_buf_size, aFile, 0);
							
							// Get Stream Encoding PTS
							if (cs->do_video) {
								e_vtime = csa->video_st->pts.val;
								e_vtime = (e_vtime/(double)csa->video_st->time_base.den);
							}
							
							e_atime = csa->audio_st->pts.val;
							e_atime = (e_atime/(double)csa->audio_st->time_base.den);
							
							// Stop Muxer
							if (!cs->do_video) {
								if (show_status)
									av_log(NULL, AV_LOG_INFO, "\n");
								stop_muxer(csa);
							} else {
								csa_count++;
								csa = &streams[csa_count%2];
							}
							audio_pts = 0;
							audio_bytes = 0;
							
							// Reinit Muxer and Encoders
							if(init_muxer(csa, o_filename))
								goto failure;
							if(cs->do_video && (init_video_encoder(csa) || open_video_encoder(csa)))
								goto failure;
							if(cs->do_audio && (init_audio_encoder(csa) || open_audio_encoder(csa)))
								goto failure;
							
							// Start Muxer
							if(start_muxer(csa, o_filename))
								goto failure;

							if (debug > 0)
								dump_format(csa->oc, audio_segment_count, o_filename, 1);
						} else {
							// Flush Audio Encoder/Fifo
							audio_bytes += flush_audio_encoder(csa, 
								audio_fifo, audio_outbuf, audio_buf_out, audio_buf_size, aFile, 0);
							
							// Get Stream Encoding PTS
							if (cs->do_video) {
								e_vtime = csa->video_st->pts.val;
								e_vtime = (e_vtime/(double)csa->video_st->time_base.den);
							}
							
							e_atime = csa->audio_st->pts.val;
							e_atime = (e_atime/(double)csa->audio_st->time_base.den);
							
							// Stop Muxer
							if (show_status)
								av_log(NULL, AV_LOG_INFO, "\n");
							stop_muxer(csa);

							csa_count++;
							csa = &streams[csa_count%2];
							cs = csa;
							audio_pts = 0;
							audio_bytes = 0;
						}
					}
					if (do_record)
						av_log(NULL, AV_LOG_WARNING, "%s[%0.2f/%0.2f] [%0.2f] Audio Segment %d [%"PRId64"] %d seconds file %s\n",
								slog, e_atime, e_vtime,
								atime, audio_segment_count, AVTimestamp.alastdts, hqvData->seconds, csa->mux?o_filename:rawpcm);
				}	
			}
		} 
	}
	// End of Main Loop
	av_log(NULL, AV_LOG_INFO, "\n");
failure:	
	// Stop and release MPEG2 DVD Muxers
	if ((hqvData->do_dvd || remux_timestamps) && pMPEGVideoFormatCtx)
		stop_dvdmux(pMPEGVideoFormatCtx);
	if (hqvData->do_dvd && pMPEGAudioFormatCtx && pMPEGVideoFormatCtx && (pMPEGVideoFormatCtx != pMPEGAudioFormatCtx))
		stop_dvdmux(pMPEGAudioFormatCtx);

	pMPEGVideoFormatCtx = pMPEGAudioFormatCtx = NULL;

	if (hqvData->do_dvd && !hqvData->keep && hqvData->archive)
		unlink(hqvData->output_tmp);

	// Signal Threads to stop
	continueDecoding = 0;
	
	// Wait for fifo and pipe to empty
	if (hqvData->do_hqv || hqvData->do_dqv || hqvData->do_dvd)
		while (!hqvData->finished) 
			usleep(100000);

	// Free fifo/pipe
	if ((hqvData->do_dqv || hqvData->do_dvd) && hqvData->fifo)
		av_fifo_free(hqvData->fifo);

	// Mux Thread
	if (mux_in_thread) {
		if (cs->do_video && rawyuv)
			sprintf(muxData->video, "%s", rawyuv);
		if (cs->do_audio && rawpcm)
			sprintf(muxData->audio, "%s", rawpcm);

		if (o_filename) {
			sprintf(muxData->muxfile, "%s", o_filename);
			muxData->muxReady = 1;
		}
	}
	
	
	// Free Video Buffers/Converter
	if (cs->do_video) {
		if (img_convert_ctx)
			sws_freeContext(img_convert_ctx);

		if (!cs->mux && vFile)
			fclose(vFile);
		
		if (encode)
			av_free(video_outbuf);
	}

	// Free Audio Buffers/Resampler
	if (cs->do_audio) {
		if (av_convert_ctx)
			audio_resample_close(av_convert_ctx);

		if (!cs->mux && aFile)
			fclose(aFile);
		
		if (encode)
			av_free(audio_outbuf);

		av_free(audio_buf_out);
		av_free(audio_buf_out_rs);
		av_fifo_free(audio_fifo);
	}

	// Free Subtitle stuff
	if (cs->do_sub) {
		if (/*!cs->mux &&*/ sFile)
			fclose(sFile);
	}

	// Close Demuxer input
	freeDemuxer(&ds);
	
	// Close Output Mux File
	if (encode) {
		if (cs->do_demux) {
			stop_muxer(csv);
			stop_muxer(csa);
		} else 
			stop_muxer(cs);
	}
	
	// Signal Thread to MUX
	if (mux_in_thread) {
		char spinner[2] = "|";
		i = 0;
		av_log(NULL, AV_LOG_INFO, "\n");
		while(muxData->muxReady) {
			av_log(NULL, AV_LOG_INFO, "\r[%"PRId64"] Muxing Video %d %s", 
						(av_gettime() / 1000000)-starttime, muxData->count, spinner);
			usleep(250000);
			if (i%2)
				strcpy(spinner, "|");
			else
				strcpy(spinner, "-");
			i++;
		}
		av_log(NULL, AV_LOG_INFO, "\n");
		
		if (muxData)
			free(muxData);
	}
	
	free(hqvData);

	// Free filter graph
	if (graph) {
        	avfilter_graph_free(graph);
		av_freep(&graph);
	}

	// Free input/output streams
	av_free(ist);
	av_free(ost);

	// Unload filters
	avfilter_uninit();
	
	return 0;
}

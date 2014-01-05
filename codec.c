#include "lav.h"
#include "codec.h"
#include "io.h"

/*******************************************************
 * CODEC SETTINGS CONFIGURATION
 ******************************************************/

void set_codec_defaults(struct CodecSettings *cs) {

        gethostname(cs->hostname, sizeof(cs->hostname));

        // Set all codec defaults to -1
        cs->ssim = -1;
        cs->psnr = -1;
        cs->do_threads = -1;
        cs->out_fps = -1;
        cs->w = -1;
        cs->h = -1;
	cs->crop_left = -1;
	cs->crop_right = -1;
	cs->crop_top = -1;
	cs->crop_bottom = -1;
        cs->bitrate = -1;
        cs->abitrate = -1;
        cs->achan = -1;
        cs->arate = -1;
        cs->sws_flags = -1;
        cs->deinterlace = -1;
        cs->do_interlace = -1;
        cs->hq = -1;
        cs->bt = -1;
        cs->qsquish = -1;
        cs->crf = -1;
        cs->cabac = -1;
        cs->wpred = -1;
        cs->weightp = -1;
        cs->mixed_refs = -1;
        cs->level = -1;
        sprintf(cs->profile, "%s", "");
        cs->fastpskip = -1;
        cs->bpyramid = -1;
        cs->aud = -1;
        cs->partitions = -1;
        cs->goplen = -1;
        cs->gop = -1;
        cs->refs = -1;
        cs->maxrate = -1;
        cs->minrate = -1;
        cs->bufsize = -1;
        cs->bframes = -1;
        cs->bstrategy = -1;
        cs->mbtree = -1;
        cs->lookahead = -1;
        cs->psy_rd = -1;
        cs->psy_trellis = -1;
        cs->aq = -1;
        cs->aq_strength = -1;
        cs->slices = -1;
        cs->trellis = -1;
        cs->nodeblock = -1;
        cs->deblocka = -99;
        cs->deblockb = -99;
        cs->subme = -1;
        cs->scthreshold = -1;
        sprintf(cs->me_method, "%s", "");
        cs->me_range = -1;
	cs->chroma_me = -1;
        cs->directpred = -1;
        cs->qcomp = -1;
        cs->nr = -1;
        cs->muxrate = -1;
        cs->packetsize = -1;
}

// Setup Codec Default Settings or Configured ones
void init_codec(CodecSettings *cs) {
        // Default Codec Settings
        if (cs->hq < 0 || cs->hq > 1)
                cs->hq = 0;
        if (cs->mux < 0)
                cs->mux = 0;
        cs->fmt = NULL;
        cs->oc = NULL;
        cs->audio_st = NULL;
        cs->video_st = NULL;
        cs->aEncCtx = NULL;
        cs->vEncCtx = NULL;
        cs->aEncCodec = NULL;
        cs->vEncCodec = NULL;
	cs->copy_audio = 0;
	cs->copy_video = 0;
	cs->sample_fmt = -1;
        if (!strcmp(cs->mux_format, ""))
                sprintf(cs->mux_format, "%s", "mp4");
        if (!strcmp(cs->audio_codec, ""))
                sprintf(cs->audio_codec, "%s", "aac");
        if (!strcmp(cs->video_codec, ""))
                sprintf(cs->video_codec, "%s", "ngtcx264");
        if (cs->ssim < 0)
                cs->ssim = 0;
        if (cs->psnr < 0)
                cs->psnr = 0;
        if (cs->out_fps < 0)
                cs->out_fps = 0;
        if (cs->bitrate < 0)
                cs->bitrate = 0;
        if (cs->do_interlace < 0)
                cs->do_interlace = 0;
        if (cs->deinterlace < 0)
                cs->deinterlace = 0;
        if (cs->bt < 0)
                cs->bt = 1.0;
        if (cs->sws_flags < 0)
                cs->sws_flags = SWS_BICUBIC;
        if (cs->qsquish < 0 || cs->qsquish > 1.0)
                cs->qsquish = 1.0;
        if (cs->w <= 0)
                cs->w = -1;
        if (cs->h <= 0)
                cs->h = -1;
        if (cs->crop_left < 0)
                cs->crop_left = 0;
        if (cs->crop_right < 0)
                cs->crop_right = 0;
        if (cs->crop_top < 0)
                cs->crop_top = 0;
        if (cs->crop_bottom < 0)
                cs->crop_bottom = 0;
        if (cs->abitrate < 0)
                cs->abitrate = 0;
        if (cs->audio_quality < 0)
                cs->audio_quality = 0;
        if (cs->achan <= 0)
                cs->achan = -1;
        if (cs->arate <= 0)
                cs->arate = -1;
        // Extended
        if (cs->level < 0)
                cs->level = 0;
        if (!strcmp(cs->profile, ""))
                sprintf(cs->profile , "%s", "high");
        if (cs->cabac < 0 || cs->cabac > 1)
                cs->cabac = 1;
        if (cs->crf < 0 || cs->crf > 51)
                cs->crf = 0;
        else if (cs->crf > 0)
                cs->bitrate = 0;
        if (cs->wpred < 0)
                cs->wpred = 1;
        if (cs->weightp < 0)
                cs->weightp = 2;
        if (cs->mixed_refs < 0)
                cs->mixed_refs = 1;
        if (cs->fastpskip < 0)
                cs->fastpskip = 1;
        if (cs->bpyramid < 0)
                cs->bpyramid = 0;
        if (cs->aud < 0)
                cs->aud = 1;
        if (cs->partitions < 0)
                cs->partitions = calc_partitions("all");
        if (cs->goplen < 0)
                cs->goplen = 0;
        if (cs->gop < 0)
                cs->gop = 12;
        if (cs->refs < 0)
                cs->refs = 4;
        if (cs->maxrate < 0)
                cs->maxrate = 0;
        if (cs->minrate < 0)
                cs->minrate = 0;
        if (cs->bufsize < 0)
                cs->bufsize = 0;
        if (cs->bframes < 0)
                cs->bframes = 16;
        if (cs->bstrategy < 0)
                cs->bstrategy = 1;
        if (cs->mbtree < 0)
                cs->mbtree = 0;
        if (cs->lookahead < 0)
                cs->lookahead = 40;
        if (cs->aq < 0)
                cs->aq = 1;
        if (cs->aq_strength < 0)
                cs->aq_strength = 1.0;
        if (cs->psy_rd < 0)
                cs->psy_rd = 1.0;
        if (cs->psy_trellis < 0)
                cs->psy_trellis = 0;
        if (cs->slices < 0)
                cs->slices = 0;
        if (cs->trellis < 0)
                cs->trellis = 0;
        if (cs->nodeblock < 0)
                cs->nodeblock = 0;
        if (cs->deblocka == -99)
                cs->deblocka = 0;
        if (cs->deblockb == -99)
                cs->deblockb = 0;
        if (cs->subme < 0)
                cs->subme = 6;
        if (cs->chroma_me < 0)
                cs->chroma_me = 1;
        if (cs->scthreshold < 0)
                cs->scthreshold = 40;
        if (!strcmp(cs->me_method, ""))
                sprintf(cs->me_method, "%s", "hex");
        if (cs->me_range < 0)
                cs->me_range = 16;
        if (cs->directpred < 0)
                cs->directpred = 3;
        if (cs->qcomp < 0 || cs->qcomp > 1)
                cs->qcomp = 0.6;
        if (cs->nr < 0)
                cs->nr = 0;
        if (cs->muxrate < 0)
                cs->muxrate = 0;
        if (cs->packetsize < 0)
                cs->packetsize = 0;
}

// Read Codec Config from File
int read_codec(CodecSettings *cs, char *filename) {
        FILE *f = NULL;
        char line[1000], tmp[1000], tmp2[1000];
        struct stat sb;

        if (!filename) {
                av_log(NULL, AV_LOG_ERROR, "Warning: No codec file found\n");
                return 0;
        }

        if (stat(filename, &sb) == -1) {
                av_log(NULL, AV_LOG_ERROR, "Error: codec file %s open failed: ", filename);
		perror(NULL);
                return -1;
        } else
                av_log(NULL, AV_LOG_VERBOSE,
                        "Opening codec file %s of %lld bytes\n", filename, sb.st_size);

        f = fopen(filename, "r");
        if(f == NULL) {
                av_log(NULL, AV_LOG_ERROR, "Error: Couldn't open codec file %s\n", filename);
                return -1;
        }

        while(!feof(f)){
                int e= fscanf(f, "%999[^\n]\n", line) - 1;
                if(line[0] == '#' && !e)
                        continue;
                e|= sscanf(line, "%999[^=]=%999[^\n]\n", tmp, tmp2) - 2;

                if(e){
                        av_log(NULL, AV_LOG_WARNING, "%s: Codec Invalid syntax: '%s'\n", filename, line);
                        continue;
                }

                if(!strcmp(tmp, "acodec")) {
                        if(!strcmp(cs->audio_codec, ""))
                                sprintf(cs->audio_codec, "%s", tmp2);
                } else if(!strcmp(tmp, "vcodec")) {
                        if(!strcmp(cs->video_codec, ""))
                                sprintf(cs->video_codec, "%s", tmp2);
                } else if(!strcmp(tmp, "format")) {
                        if(!strcmp(cs->mux_format, ""))
                                sprintf(cs->mux_format, "%s", tmp2);
                } else if(!strcmp(tmp, "ssim")) {
                        if(cs->ssim < 0)
                                cs->ssim = atoi(tmp2);
                } else if(!strcmp(tmp, "psnr")) {
                        if(cs->psnr < 0)
                                cs->psnr = atoi(tmp2);
                } else if(!strcmp(tmp, "threads")) {
                        if(cs->do_threads < 0)
                                cs->do_threads = atoi(tmp2);
                } else if(!strcmp(tmp, "fps")) {
                        if((cs->out_fps < 0 || cs->out_fps > 60))
                                cs->out_fps = atof(tmp2);
                } else if(!strcmp(tmp, "bitrate")) {
                        if(cs->bitrate < 0)
                                cs->bitrate = atoi(tmp2);
                } else if(!strcmp(tmp, "abitrate")) {
                        if(cs->abitrate < 0)
                                cs->abitrate = atoi(tmp2);
                } else if(!strcmp(tmp, "audio_quality")) {
                        if(cs->audio_quality < 0)
                                cs->audio_quality = atoi(tmp2);
                } else if(!strcmp(tmp, "arate")) {
                        if(cs->arate < 0)
                                cs->arate = atoi(tmp2);
                } else if(!strcmp(tmp, "achan")) {
                        if(cs->achan < 0)
                                cs->achan = atoi(tmp2);
                } else if(!strcmp(tmp, "w")) {
                        if(cs->w < 0)
                                cs->w = atoi(tmp2);
                } else if(!strcmp(tmp, "h")) {
                        if(cs->h < 0)
                                cs->h = atoi(tmp2);
                } else if(!strcmp(tmp, "crop_left")) {
                        if(cs->crop_left < 0)
                                cs->crop_left = atoi(tmp2);
                } else if(!strcmp(tmp, "crop_right")) {
                        if(cs->crop_right < 0)
                                cs->crop_right = atoi(tmp2);
                } else if(!strcmp(tmp, "crop_top")) {
                        if(cs->crop_top < 0)
                                cs->crop_top = atoi(tmp2);
                } else if(!strcmp(tmp, "crop_bottom")) {
                        if(cs->crop_bottom < 0)
                                cs->crop_bottom = atoi(tmp2);
                } else if(!strcmp(tmp, "hq")) {
                        if(cs->hq < 0 || cs->hq > 1)
                                cs->hq = atoi(tmp2);
                } else if(!strcmp(tmp, "interlace")) {
                        if (cs->do_interlace < 0 || cs->do_interlace > 1)
                                cs->do_interlace = atoi(tmp2);
                } else if(!strcmp(tmp, "deinterlace")) {
                        if (cs->deinterlace < 0 || cs->deinterlace > 1)
                                cs->deinterlace = atoi(tmp2);
                } else if(!strcmp(tmp, "crf")) {
                        if (cs->crf < 0)
                                cs->crf = atoi(tmp2);
                } else if(!strcmp(tmp, "bt")) {
                        if (cs->bt < 0)
                                cs->bt = atof(tmp2);
                } else if(!strcmp(tmp, "sws")) {
                        if (cs->sws_flags < 0)
                                cs->sws_flags = calc_sws(strtol(tmp2, NULL, 10));
                } else if(!strcmp(tmp, "qsquish")) {
                        if (cs->qsquish < 0)
                                cs->qsquish = atof(tmp2);
                } else if(!strcmp(tmp, "cabac")) {
                        if (cs->cabac < 0)
                                cs->cabac = atoi(tmp2);
                } else if(!strcmp(tmp, "wpred")) {
                        if (cs->wpred < 0)
                                cs->wpred = atoi(tmp2);
                } else if(!strcmp(tmp, "weightp")) {
                        if (cs->weightp < 0)
                                cs->weightp = atoi(tmp2);
                } else if(!strcmp(tmp, "mixed_refs")) {
                        if (cs->mixed_refs < 0)
                                cs->mixed_refs = atoi(tmp2);
                } else if(!strcmp(tmp, "level")) {
                        if (cs->level < 0)
                                cs->level = atof(tmp2);
                } else if(!strcmp(tmp, "profile")) {
                        if (!strcmp(cs->profile, ""))
                                sprintf(cs->profile, "%s", tmp2);
                } else if(!strcmp(tmp, "fastpskip")) {
                        if (cs->fastpskip < 0)
                                cs->fastpskip = atoi(tmp2);
                } else if(!strcmp(tmp, "bpyramid")) {
                        if (cs->bpyramid < 0)
                                cs->bpyramid = atoi(tmp2);
                } else if(!strcmp(tmp, "aud")) {
                        if (cs->aud < 0)
                                cs->aud = atoi(tmp2);
                } else if(!strcmp(tmp, "partitions")) {
                        if (cs->partitions < 0)
                                cs->partitions = calc_partitions(tmp2);
                } else if(!strcmp(tmp, "goplen")) {
                        if (cs->goplen < 0)
                                cs->goplen = atoi(tmp2);
                } else if(!strcmp(tmp, "gop")) {
                        if (cs->gop < 0)
                                cs->gop = atoi(tmp2);
                } else if(!strcmp(tmp, "refs")) {
                        if (cs->refs < 0)
                                cs->refs = atoi(tmp2);
                } else if(!strcmp(tmp, "maxrate")) {
                        if (cs->maxrate < 0)
                                cs->maxrate = atoi(tmp2);
                } else if(!strcmp(tmp, "minrate")) {
                        if (cs->minrate < 0)
                                cs->minrate = atoi(tmp2);
                } else if(!strcmp(tmp, "bufsize")) {
                        if (cs->bufsize < 0)
                                cs->bufsize = atoi(tmp2);
                } else if(!strcmp(tmp, "bframes")) {
                        if (cs->bframes < 0)
                                cs->bframes = atoi(tmp2);
                } else if(!strcmp(tmp, "bstrategy")) {
                        if (cs->bstrategy < 0)
                                cs->bstrategy = atoi(tmp2);
                } else if(!strcmp(tmp, "mbtree")) {
                        if (cs->mbtree < 0)
                                cs->mbtree = atoi(tmp2);
                } else if(!strcmp(tmp, "lookahead")) {
                        if (cs->lookahead < 0)
                                cs->lookahead = atoi(tmp2);
                } else if(!strcmp(tmp, "aq")) {
                        if (cs->aq < 0)
                                cs->aq = atoi(tmp2);
                } else if(!strcmp(tmp, "aq_strength")) {
                        if (cs->aq_strength < 0)
                                cs->aq_strength = atof(tmp2);
                } else if(!strcmp(tmp, "psy_rd")) {
                        if (cs->psy_rd < 0)
                                cs->psy_rd = atof(tmp2);
                } else if(!strcmp(tmp, "psy_trellis")) {
                        if (cs->psy_trellis < 0)
                                cs->psy_trellis = atof(tmp2);
                } else if(!strcmp(tmp, "slices")) {
                        if (cs->slices < 0)
                                cs->slices = atoi(tmp2);
                } else if(!strcmp(tmp, "trellis")) {
                        if (cs->trellis < 0)
                                cs->trellis = atoi(tmp2);
                } else if(!strcmp(tmp, "nodeblock")) {
                        if (cs->nodeblock < 0)
                                cs->nodeblock = atoi(tmp2);
                } else if(!strcmp(tmp, "deblocka")) {
                        if (cs->deblocka == -99)
                                cs->deblocka = atoi(tmp2);
                } else if(!strcmp(tmp, "deblockb")) {
                        if (cs->deblockb == -99)
                                cs->deblockb = atoi(tmp2);
                } else if(!strcmp(tmp, "subme")) {
                        if (cs->subme < 0)
                                cs->subme = atoi(tmp2);
                } else if(!strcmp(tmp, "chroma_me")) {
                        if (cs->chroma_me < 0)
                                cs->chroma_me = atoi(tmp2);
                } else if(!strcmp(tmp, "scthreshold")) {
                        if (cs->scthreshold < 0)
                                cs->scthreshold = atoi(tmp2);
                } else if(!strcmp(tmp, "me_method")) {
                        if (!strcmp(cs->me_method, ""))
                                sprintf(cs->me_method, "%s", tmp2);
                } else if(!strcmp(tmp, "me_range")) {
                        if (cs->me_range < 0)
                                cs->me_range = atoi(tmp2);
                } else if(!strcmp(tmp, "directpred")) {
                        if (cs->directpred < 0)
                                cs->directpred = atoi(tmp2);
                } else if(!strcmp(tmp, "qcomp")) {
                        if (cs->qcomp < 0)
                                cs->qcomp = atof(tmp2);
                } else if(!strcmp(tmp, "nr")) {
                        if (cs->nr < 0)
                                cs->nr = atoi(tmp2);
                } else if(!strcmp(tmp, "muxrate")) {
                        if (cs->muxrate < 0)
                                cs->muxrate = atoi(tmp2);
                } else if(!strcmp(tmp, "packetsize")) {
                        if (cs->packetsize < 0)
                                cs->packetsize = atoi(tmp2);
                } else {
                        av_log(NULL, AV_LOG_WARNING, 
				"%s: Codec setting ignored: '%s', '%s' = '%s'\n",
                                        filename, line, tmp, tmp2);
                }
        }

        fclose(f);

        return 0;
}


#include "lav.h"

void print_all_lib_versions(FILE* outstream, int indent)
{
    unsigned int version;
    PRINT_LIB_VERSION(outstream, avutil,   AVUTIL,   indent);
    PRINT_LIB_VERSION(outstream, avcodec,  AVCODEC,  indent);
    PRINT_LIB_VERSION(outstream, avformat, AVFORMAT, indent);
    PRINT_LIB_VERSION(outstream, avfilter,  AVFILTER,  indent);
    PRINT_LIB_VERSION(outstream, swscale,  SWSCALE,  indent);
}

int calc_partitions(char *partitions) {
        int pt = 0x00;

        if (!strcmp(partitions, "all"))
                return (X264_PART_I4X4|X264_PART_I8X8|X264_PART_P8X8|X264_PART_P4X4|X264_PART_B8X8);
        else if (!strcmp(partitions, "none"))
                return 0x00; // NONE

        if (strstr(partitions, "i4x4"))
                pt |= X264_PART_I4X4;
        if (strstr(partitions, "i8x8"))
                pt |= X264_PART_I8X8;
        if (strstr(partitions, "p8x8"))
                pt |= X264_PART_P8X8;
        if (strstr(partitions, "p4x4"))
                pt |= X264_PART_P4X4;
        if (strstr(partitions, "b8x8"))
                pt |= X264_PART_B8X8;

        return pt;
}

// Motion Estimation Mode
int calc_me(char *method) {
        int mode = ME_HEX;

        if (!strcmp(method, "zero")) {
                mode = ME_ZERO;
        } else if (!strcmp(method, "full")) {
                mode = ME_FULL;
        } else if (!strcmp(method, "log")) {
                mode = ME_LOG;
        } else if (!strcmp(method, "phods")) {
                mode = ME_PHODS;
        } else if (!strcmp(method, "epzs")) {
                mode = ME_EPZS;
        } else if (!strcmp(method, "x1")) {
                mode = ME_X1;
        } else if (!strcmp(method, "hex")) {
                mode = ME_HEX;
        } else if (!strcmp(method, "umh")) {
                mode = ME_UMH;
        } else if (!strcmp(method, "iter")) {
                mode = ME_ITER;
        } else if (!strcmp(method, "tesa")) {
                mode = ME_TESA;
        } else
                av_log(NULL, AV_LOG_ERROR, "Warning: Unknown motion estimation mode %s\n", method);
        return mode;
}

// Software Scaling Mode
int calc_sws(int sws_flags) {
        int val = SWS_BICUBIC;

        switch (sws_flags) {
                case 0:
                       val = SWS_FAST_BILINEAR;
                       break;
                case 1:
                       val = SWS_BILINEAR;
                       break;
                case 2:
                       val = SWS_BICUBIC;
                       break;
                case 3:
                       val = SWS_X;
                       break;
                case 4:
                       val = SWS_POINT;
                       break;
                case 5:
                       val = SWS_AREA;
                       break;
                case 6:
                       val = SWS_BICUBLIN;
                       break;
                case 7:
                       val = SWS_GAUSS;
                       break;
                case 8:
                       val = SWS_SINC;
                       break;
                case 9:
                       val = SWS_LANCZOS;
                       break;
                case 10:
                       val = SWS_SPLINE;
                       break;
                default:
                       val = SWS_BICUBIC;
        }

        return val;
}

void show_formats(void)
{
    AVInputFormat *ifmt=NULL;
    AVOutputFormat *ofmt=NULL;
    URLProtocol *up=NULL;
    AVCodec *p=NULL, *p2;
    AVBitStreamFilter *bsf=NULL;
    const char *last_name;

    printf(
        "File formats:\n"
        " D. = Demuxing supported\n"
        " .E = Muxing supported\n"
        " --\n");
    last_name= "000";
    for(;;){
        int decode=0;
        int encode=0;
        const char *name=NULL;
        const char *long_name=NULL;

        while((ofmt= av_oformat_next(ofmt))) {
            if((name == NULL || strcmp(ofmt->name, name)<0) &&
                strcmp(ofmt->name, last_name)>0){
                name= ofmt->name;
                long_name= ofmt->long_name;
                encode=1;
            }
        }
        while((ifmt= av_iformat_next(ifmt))) {
            if((name == NULL || strcmp(ifmt->name, name)<0) &&
                strcmp(ifmt->name, last_name)>0){
                name= ifmt->name;
                long_name= ifmt->long_name;
                encode=0;
            }
            if(name && strcmp(ifmt->name, name)==0)
                decode=1;
        }
        if(name==NULL)
            break;
        last_name= name;

        printf(
            " %s%s %-15s %s\n",
            decode ? "D":" ",
            encode ? "E":" ",
            name,
            long_name ? long_name:" ");
    }
    printf("\n");

    printf(
        "Codecs:\n"
        " D..... = Decoding supported\n"
        " .E.... = Encoding supported\n"
        " ..V... = Video codec\n"
        " ..A... = Audio codec\n"
        " ..S... = Subtitle codec\n"
        " ...S.. = Supports draw_horiz_band\n"
        " ....D. = Supports direct rendering method 1\n"
        " .....T = Supports weird frame truncation\n"
        " ------\n");
    last_name= "000";
    for(;;){
        int decode=0;
        int encode=0;
        int cap=0;
        const char *type_str;

        p2=NULL;
        while((p= av_codec_next(p))) {
            if((p2==NULL || strcmp(p->name, p2->name)<0) &&
                strcmp(p->name, last_name)>0){
                p2= p;
                decode= encode= cap=0;
            }
            if(p2 && strcmp(p->name, p2->name)==0){
                if(p->decode) decode=1;
                if(p->encode) encode=1;
                cap |= p->capabilities;
            }
        }
        if(p2==NULL)
            break;
        last_name= p2->name;

        switch(p2->type) {
        case CODEC_TYPE_VIDEO:
            type_str = "V";
            break;
        case CODEC_TYPE_AUDIO:
            type_str = "A";
            break;
        case CODEC_TYPE_SUBTITLE:
            type_str = "S";
            break;
        default:
            type_str = "?";
            break;
        }
        printf(
            " %s%s%s%s%s%s %-15s %s",
            decode ? "D": (/*p2->decoder ? "d":*/" "),
            encode ? "E":" ",
            type_str,
            cap & CODEC_CAP_DRAW_HORIZ_BAND ? "S":" ",
            cap & CODEC_CAP_DR1 ? "D":" ",
            cap & CODEC_CAP_TRUNCATED ? "T":" ",
            p2->name,
            p2->long_name ? p2->long_name : "");
       /* if(p2->decoder && decode==0)
            printf(" use %s for decoding", p2->decoder->name);*/
        printf("\n");
    }
    printf("\n");

    printf("Bitstream filters:\n");
    while((bsf = av_bitstream_filter_next(bsf)))
        printf(" %s", bsf->name);
    printf("\n");

    printf("Supported file protocols:\n");
    while((up = av_protocol_next(up)))
        printf(" %s:", up->name);
    printf("\n");
}


#ifndef LAV_H
#define LAV_H


#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/fifo.h>
#include <libavutil/log.h>
#include <libavcodec/opt.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/avfiltergraph.h>


#define PRINT_LIB_VERSION(outstream,libname,LIBNAME,indent) \
    version= libname##_version(); \
    fprintf(outstream, "%slib%-10s %02d.%02d.%02d / %02d.%02d.%02d\n", indent? "  " : "", #libname, \
            LIB##LIBNAME##_VERSION_MAJOR, LIB##LIBNAME##_VERSION_MINOR, LIB##LIBNAME##_VERSION_MICRO, \
            version >> 16, version >> 8 & 0xff, version & 0xff);

void print_all_lib_versions(FILE* outstream, int indent);
int calc_partitions(char *partitions);
int calc_me(char *method);
int calc_sws(int sws_flags);
void show_formats(void);

struct SwsContext *sws_opts;

#endif


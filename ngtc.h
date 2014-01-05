#ifndef NGTC_H
#define NGTC_H

#include "version.h"

#define DQV_DIR  "/data/mpegout"
#define HQV_DIR  "/data/hqv"
#define HQV_TMP  "/data/tmp"

#define INPUT_FIFO_SIZE 16*(1024*1024)
#define STREAM_BUFFER_SIZE 32768

#define MAX_AUDIO_PACKET_SIZE (128 * 1024)

int continueDecoding;

int debug;
char slog[30];
char elog[30];

int split_string(char *filename, char *base, char *ext, char sep);
char *current_date(char *date);

#endif

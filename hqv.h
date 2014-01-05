#ifndef HQV_H
#define HQV_H

#include <unistd.h>
#include <time.h>
#include <libavutil/fifo.h>
#include <libavutil/log.h>
#include "io.h"

// HQV Date Structure
struct hqv_data {
        char source[512];
        char output[512];
        char output_tmp[512];
        char *channel;
        int device;
        int stationid;
        // settings
        int seconds;
        int offset;
        int read_size;
        int round;
        int record;
        int em;
        char em_range[8];
        int splice;
        int align;
        int keep;
        int archive;
        int cc;
        // GOP Splicing
        unsigned int last_stream;
        int try_count;
        int loops;
        unsigned int nxt_buf_start_pos;
        int audio_frames;
        int parse_mpeg2;
        int break_type; // 0=none, 1=audio, 2=video
        // counter
        int count;
        // DQV/HQV mode
        int do_dqv;
        int do_hqv;
        int do_dvd;
        // Signal next segment
        int next_file;
        // Input file pos
        int64_t brfp;
        // Fifo
        AVFifoBuffer *fifo;
        // End of HQV
        int finished;
        // Fifo Queue
        int fifo_full;
};

void set_hqv_defaults(struct hqv_data *hqvData);
void align_time(int seconds, int offset);
int check_em(struct hqv_data *hqv, struct tm *t, int state);
int fifo_read(void *data, uint8_t *buf, int size);
void *hqv_thread(void *data);

char *dqv_get(char *dqout, char *hostname, char *channel, int dev, char *ext, int round_seconds, char *dir);
char *hqv_get(char *dqout, char *hostname, char *channel, int dev, char *ext, int round_seconds, char *dir);
char *sdc_get(char *dqout, char *hostname, char *channel, int dev, char *ext, int round_seconds, char *dir);

#endif

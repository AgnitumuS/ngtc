#ifndef MP4BOX_H
#define MP4BOX_H

void *mux_thread(void *data);

struct mux_data {
        char video[512];
        char audio[512];
        char muxfile[512];
        double fps;
        int count;
        int do_audio;
        int do_video;
        int muxReady;
};

#endif

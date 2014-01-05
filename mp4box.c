#include <libavutil/log.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#include "ngtc.h"
#include "mp4box.h"

void *mux_thread(void *data)
{
        struct mux_data *md = (struct mux_data*)data;
        char mp4box[30] = "/usr/local/bin/MP4Box";
        char cmdLine[512] = "";
        char quiet[10] = "";
        char redir[30] = "2>&1 >/dev/null";

        av_log(NULL, AV_LOG_INFO, "Starting Mux Thread...\n");

        while (continueDecoding) {
                if (md->muxReady) {
                        av_log(NULL, AV_LOG_INFO,  "Starting Mux...\n");
                        if ((!md->do_video || md->video != NULL) && (!md->do_audio || md->audio != NULL) && md->muxfile != NULL) {
                                char vline[512] = "";
                                char aline[512] = "";
                                char fpsline[50] = "";
                                if (debug <= 0)
                                        strcpy(quiet, "-quiet");
                                else if (debug > 0) {
                                        strcpy(quiet, "-v");
                                        strcpy(redir, "");
                                }
                                if (md->do_video) {
                                        sprintf(vline, "-add %s#video", md->video);
                                        sprintf(fpsline, "-fps %0.3f", md->fps);
                                }
                                if (md->do_audio)
                                        sprintf(aline, "-add %s#audio", md->audio);

                                sprintf(cmdLine, "%s %s -inter 1000 %s %s %s -new %s %s",
                                                mp4box, quiet, fpsline, vline, aline, md->muxfile, redir);
                                av_log(NULL, AV_LOG_INFO,  "Running Muxer cmd: %s\n", cmdLine);
                                int ret = system(cmdLine);
                                if (WIFSIGNALED(ret) &&
                                        (WTERMSIG(ret) == SIGINT || WTERMSIG(ret) == SIGQUIT))
                                        av_log(NULL, AV_LOG_FATAL,  "Error running Muxer command.\n");
                                unlink(md->video);
                                unlink(md->audio);
                        } else
                                av_log(NULL, AV_LOG_FATAL, "Error with Mux Thread, video/audio/muxfile are NULL %s/%s/%s.\n",
                                                md->video, md->audio, md->muxfile);

                        md->muxReady = 0;
                        md->count++;
                }
                usleep(250000);
        }
        md->muxReady = 0;

        return 0;
}


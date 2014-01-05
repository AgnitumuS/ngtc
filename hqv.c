#include <libavformat/avio.h>
#include "hqv.h"
#include "mpeg.h"
#include "ngtc.h"

void set_hqv_defaults(struct hqv_data *hqvData) {
        // Set hqv structure defaults
        hqvData->fifo = NULL;
        hqvData->fifo_full = 0;
        hqvData->record = 1;
        hqvData->archive = 1;
        hqvData->keep = 0;
        hqvData->finished = 0;
        hqvData->channel = NULL;
        hqvData->stationid = 0;
        hqvData->cc = 0;
        hqvData->read_size = 128 * 1024;
        hqvData->splice = 1;
        hqvData->round = 1;
        hqvData->align = 1;
        hqvData->next_file = 0;
        hqvData->brfp = 0;
        sprintf(hqvData->em_range, "%s", "1-5");
}

/**************************/
/*  HQV Functions         */
/**************************/

// Align startup time to clock
void align_time(int seconds, int offset) {
        time_t t;
        struct tm tmbase;
        double ms = 0.0;

        av_log(NULL, AV_LOG_INFO, "\n\r[0.00] Aligning time to %d seconds...", seconds);

        while ((t = time(0)) % (seconds)) {
                if (offset) {
                        localtime_r(&t, &tmbase);

                        if (tmbase.tm_min == (60 - offset))
                                break;
                }

                if (!continueDecoding)
                        break;

                usleep(250000);
                ms+=.250;
                av_log(NULL, AV_LOG_INFO, "\r[%0.2f] Aligning time to %d seconds...",
                                        ms, seconds);
        }
        av_log(NULL, AV_LOG_INFO, "\r[%0.2f] Aligned time to %d seconds, starting capture.\n",
                                ms, seconds);

        return;
}

// Extended Mode Check "00:57-04:48"
int check_em(struct hqv_data *hqv, struct tm *t, int state) {
        int mode = 1;
        char st[8] = "";
        char et[8] = "";
        int a = 0;
        int b = 4;

        split_string(hqv->em_range, st, et, '-');
        a = atoi(st)-1;
        b = atoi(et)-1;

        // Turn off recording inside extended mode
        if (((t->tm_hour == a && t->tm_min >= 57) && hqv->offset) || // sdc and 0:58+
                (t->tm_hour > a && t->tm_hour < b) || // 1:00-3:00
                ((t->tm_hour == b && t->tm_min <= 48)) // 4:00-4:50
                )
        {
                // Turn off capture
                if (state != 0)
                        av_log(NULL, AV_LOG_WARNING, "\rTurning Capture OFF, not in extended mode %d-%d\n", a+1, b+1);
                mode = 0;
        } else {
                // Turn on capture
                if (state != 1)
                        av_log(NULL, AV_LOG_WARNING, "\rTurning Capture ON, outside extended mode %d-%d\n", a+1, b+1);
                mode = 1;
        }
        return mode;
}

int fifo_read(void *data, uint8_t *buf, int size) {
        struct hqv_data *hqv = (struct hqv_data*)data;
        int fsize;

        if (!hqv->fifo) {
                av_log(NULL, AV_LOG_ERROR,
                        "\rHQV Fifo Read Error, fifo doesn't exist\n");
                return -1;
        }

        // Wait for data in fifo
        while ((fsize = av_fifo_size(hqv->fifo)) < size && !hqv->finished)
                usleep(33000);

        av_log(NULL, AV_LOG_DEBUG,
                "\rHQV Fifo Read size %d fifo size %d %s\n",
                        size, fsize, hqv->finished?"EOS":"");

        // End of stream
        if (fsize <= 0 && hqv->finished)
                return URL_EOF;

        // Empty or error
        if (fsize <= 0) {
                av_log(NULL, AV_LOG_WARNING,
                        "\rHQV Fifo Read, fifo returned %d\n", fsize);
                return fsize;
        }
        if (fsize > size)
                fsize = size;

        // Read from fifo into buffer
        av_fifo_generic_read(hqv->fifo, buf, fsize, NULL);

        return fsize;
}

void *hqv_thread(void *data) {
        struct hqv_data *hqv = (struct hqv_data*)data;
        time_t st = 0, tt = 0, tstamp = 0;
        struct tm t;
        int fd, secs, data_size;
        FILE *hqvFile = NULL;
        unsigned char *buffer = NULL;
        int splice_pos = 0;
        int64_t bytes_written_tf = 0;
        int64_t bytes_written_tp = 0;
        int64_t bytes_read = 0;
        size_t ret;
        char hostname[256];
        int read_failure = 0;

        gethostname(hostname, sizeof(hostname));

        if (hqv->read_size > 0)
                buffer = (unsigned char*)malloc(hqv->read_size+1);
        if (buffer == NULL) {
                av_log(NULL, AV_LOG_FATAL, "\rError: Couldn't allocate HQV Buffer\n");
                return NULL;
        }

        // Turn on MPEG2 parser
        if (hqv->do_hqv && !hqv->do_dvd && hqv->splice) {
                hqv->parse_mpeg2 = 1;
                //hqv->break_type = 1; // 0=none, 1=audio, 2=video
        }

        // Open Input
        fd = open_source(hqv->source);

        // Get Start Time
        st = tt = time(0);

        // Check Time
        tstamp = time(NULL);
        localtime_r(&tstamp, &t);

        // Turn off recording inside extended mode
        if (!hqv->em && hqv->record)
                if (!check_em(hqv, &t, hqv->record))
                        hqv->record = 0;

        // Get filename
        if (hqv->offset && (hqv->do_hqv || hqv->do_dvd))
                sdc_get(hqv->output, hostname, hqv->channel, hqv->device, "mpg", hqv->round, HQV_DIR);
        else if ((hqv->do_hqv || hqv->do_dvd) && hqv->archive)
                hqv_get(hqv->output, hostname, hqv->channel, hqv->device, "mpg", hqv->round, HQV_DIR);
        else if (hqv->do_hqv || hqv->do_dvd)
                dqv_get(hqv->output, hostname, hqv->channel, hqv->device, "mpg", hqv->round, HQV_TMP);

        if (hqv->do_hqv || hqv->do_dvd)
                dqv_get(hqv->output_tmp, hostname, hqv->channel, hqv->device, "mpg", hqv->round, HQV_TMP);

        if (hqv->record && (hqv->do_hqv || hqv->do_dvd))
                av_log(NULL, AV_LOG_WARNING, "\r[%d] HQV File: %s.\n", hqv->count, hqv->output);

        // Open first HQV output file
        if (hqv->record && hqv->do_hqv && !hqv->do_dvd) {
                hqvFile = fopen(hqv->output, "wb");
                if (hqvFile == NULL) {
                        av_log(NULL, AV_LOG_FATAL, "\rError: Unable to open HQV output file %s.\n",
                                        hqv->output);
                        return NULL;
                }
        }
        if (hqv->record && hqv->archive && hqv->do_hqv)
                if (!hqv->do_dvd) {
                        if (link(hqv->output, hqv->output_tmp)) {
                        	av_log(NULL, AV_LOG_FATAL, "\rError: Unable to link output file %s.\n",
                                        hqv->output);
				return NULL;
			}

		}

        // HQV Loop
        while (1) {
                int continue_decoding = continueDecoding;

                // Read from device
                if ((data_size = read_source(fd, buffer, hqv->read_size)) <= 0) {
                        if (data_size == 0) {
				read_failure++;

				if (read_failure > 30) {
                			av_log(NULL, AV_LOG_FATAL, 
						"\rERROR: Failed getting data from device for HQV File: %s.\n", 
						hqv->output);
					break;
				}

                                usleep(100000);
                                continue;
                        } else
                                break;
                } else
                        bytes_read += data_size;

		read_failure = 0;

                // Parse MPEG2
                if (hqv->splice && hqv->parse_mpeg2)
                        splice_mpeg2(hqv, buffer, data_size, hqv->break_type, 0);

                // Current Seconds
                tt = time(0);
                secs = tt - st;

                // Check Time
                tstamp = time(NULL);
                localtime_r(&tstamp, &t);

                // Check if segment is complete
                if (!continue_decoding || secs >= hqv->seconds
                        // HQV/DQV Mode
                        || (!hqv->offset && t.tm_min != 0
                                && (t.tm_min%(hqv->seconds/60)) == 0
                                && secs > (hqv->seconds-10))
                        // SDC Mode
                        || (hqv->offset && t.tm_min == ((hqv->seconds/60)-hqv->offset)
                                && (secs > (3*((hqv->offset*60)+60)))))
                {
                        int exit_em = 0;
                        int enter_em = 0;

                        // Turn off recording inside extended mode
                        if (!hqv->em && hqv->record) {
                                if (!check_em(hqv, &t, hqv->record))
                                        enter_em = 1;
                        } else if (!hqv->em && !hqv->record) {
                                if (check_em(hqv, &t, hqv->record)) {
                                        hqv->record = 1;
                                        exit_em = 1;
                                }
                        }

                        // Signal DQV thread to start a new segment
                        if (continue_decoding) {
                                // New filename
                                if (hqv->offset && (hqv->do_hqv || hqv->do_dvd))
                                        sdc_get(hqv->output, hostname, hqv->channel, hqv->device, "mpg", hqv->round, HQV_DIR);
                                else if ((hqv->do_hqv || hqv->do_dvd) && hqv->archive)
                                        hqv_get(hqv->output, hostname, hqv->channel, hqv->device, "mpg", hqv->round, HQV_DIR);
                                else if (hqv->do_hqv || hqv->do_dvd)
                                        dqv_get(hqv->output, hostname, hqv->channel, hqv->device, "mpg", hqv->round, HQV_TMP);

                                if (!hqv->keep && hqv->archive && (hqvFile || hqv->do_dvd))
                                        unlink(hqv->output_tmp);

                                // Tmp file
                                if (hqv->do_hqv || hqv->do_dvd)
                                        dqv_get(hqv->output_tmp, hostname, hqv->channel, hqv->device, "mpg", hqv->round, HQV_TMP);

                                // Signal DQV/DVD to switch
                                hqv->next_file = 1;
                        }

                        if (hqv->record) {
                                if (!hqv->parse_mpeg2 && hqv->splice)
                                        splice_init(hqv);
                                if (hqv->splice)
                                        splice_pos = -1;
                                hqv->try_count = 0;
                                hqv->loops = 0;
                                while ((!hqv->do_dvd || !continue_decoding || enter_em || exit_em) && hqv->splice && hqv->try_count < 100) {
                                        if (hqv->try_count > 20) // Give up on break type
                                                hqv->break_type = 0;
                                        if (data_size > 0)
                                                splice_pos = splice_mpeg2(hqv, buffer, data_size, hqv->break_type, 1);
                                        if (splice_pos < 0) {
                                                // Write to files
                                                if (data_size > 0) {
                                                        // HQV Mode
                                                        if (hqv->do_hqv && hqvFile) {
                                                                ret = fwrite(buffer, 1, data_size, hqvFile);
                                                                if (ret > 0)
                                                                        bytes_written_tf += ret;
                                                                if (ret != data_size)
                                                                        av_log(NULL, AV_LOG_ERROR, "\rHQV File Write Error %d bytes of %d bytes written\n", ret, data_size);
                                                        }

                                                        // DQV Mode?
                                                        if ((hqv->do_dqv || hqv->do_dvd) && !exit_em) {
                                                                if (av_fifo_size(hqv->fifo) >= INPUT_FIFO_SIZE)
                                                                        av_log(NULL, AV_LOG_ERROR, "\rHQV fifo currently full with %d bytes\n", av_fifo_size(hqv->fifo));
                                                                else {
                                                                        ret = av_fifo_generic_write(hqv->fifo, (uint8_t *)buffer, data_size, NULL);
                                                                        if (ret > 0)
                                                                                bytes_written_tp += ret;
                                                                        if (ret != data_size)
                                                                                av_log(NULL, AV_LOG_ERROR,"\rHQV fifo Write Error %d bytes of %d bytes written\n", ret, data_size);
                                                                }
                                                        }
                                                }

                                                // Read from device
                                                if ((data_size = read_source(fd, buffer, hqv->read_size)) == 0)
                                                        continue;
                                                if (data_size < 0)
                                                        break;
                                                bytes_read += data_size;
                                        } else
                                                break; // Found GOP
                                }
                                if (data_size <= 0) {
                                        // failure to read from device
                                        av_log(NULL, AV_LOG_FATAL, "\rError: Error reading from device after %d trys, exiting. %d\n", hqv->try_count, data_size);
                                        break;
                                }

                                if ((!hqv->do_dvd || !continue_decoding || enter_em || exit_em) && hqv->splice && splice_pos < 0) {
                                        // failure to find GOP
                                        av_log(NULL, AV_LOG_FATAL, "\rError: GOP never found after %d trys, exiting. %d\n", hqv->try_count, splice_pos);
                                        break; // Failed
                                }

                                // Write out end of GOP
                                if (hqv->splice && hqv->do_hqv && hqvFile && splice_pos > 0) {
                                        ret = fwrite(buffer, 1, splice_pos, hqvFile);
                                        if (ret > 0)
                                                bytes_written_tf += ret;
                                        if (ret != splice_pos)
                                                av_log(NULL, AV_LOG_ERROR,"\rHQV File Write Error %d bytes of %d bytes written\n", ret, splice_pos);
                                }

                                // Parse MPEG2
                                if (hqv->parse_mpeg2 && hqv->splice && splice_pos >= 0)
                                        splice_mpeg2(hqv, buffer+splice_pos, data_size-splice_pos, hqv->break_type, 0);

                                // Write to encoder if exiting program
                                if (!continue_decoding && (hqv->do_dqv || hqv->do_dvd) && !exit_em && splice_pos > 0) {
                                        if (av_fifo_size(hqv->fifo) >= INPUT_FIFO_SIZE)
                                                av_log(NULL, AV_LOG_ERROR, "\rHQV fifo currently full with %d bytes\n", av_fifo_size(hqv->fifo));
                                        else {
                                                ret = av_fifo_generic_write(hqv->fifo, (uint8_t *)buffer, splice_pos, NULL);
                                                if (ret > 0)
                                                        bytes_written_tp += ret;
                                                if (ret != splice_pos)
                                                        av_log(NULL, AV_LOG_ERROR, "\rHQV fifo Write Error %d bytes of %d bytes written\n", ret, splice_pos);
                                        }
                                        av_log(NULL, AV_LOG_INFO, "\rHQV fifo last Write of %d bytes\n", splice_pos);
                                }

                                // Close current HQV output file
                                if (hqvFile) {
                                        fclose(hqvFile);
                                }
                                hqvFile = NULL;

                                // Output stats for segment
                                if (hqv->record && (hqv->do_hqv || hqv->do_dvd)) {
                                        av_log(NULL, AV_LOG_WARNING,
                                                "%s#%d %d %s [r %"PRId64"/%"PRId64"/%"PRId64"] [w %"PRId64"/%"PRId64"] [d %"PRId64"]\n",
                                                slog, hqv->count, secs, hqv->output,
                                                bytes_read, (hqv->do_hqv && !hqv->do_dvd)?(bytes_read-(data_size-splice_pos)):0, (hqv->do_dqv || hqv->do_dvd)?(bytes_read-data_size):0,
                                                bytes_written_tf, bytes_written_tp,
                                                hqv->brfp);
                                }

                                // If signaled then exit
                                if (!continue_decoding)
                                        break;

                                // Enter extended mode
                                if (enter_em)
                                        hqv->record = 0;

                                // Open new HQV output file
                                if (hqv->record && hqv->do_hqv && !hqv->do_dvd) {
                                        hqvFile = fopen(hqv->output, "wb");
                                        if (hqvFile == NULL) {
                                                av_log(NULL, AV_LOG_FATAL,
                                                        "\rError: Unable to open HQV output file %s.\n",
                                                        hqv->output);
                                                break;
                                        }
                                }
                                if (hqv->record && hqv->do_hqv && hqv->archive)
                                        if (!hqv->do_dvd) {
                                                if (link(hqv->output, hqv->output_tmp)) {
                                                	av_log(NULL, AV_LOG_FATAL,
                                                        	"\rError: Unable to link output file %s.\n",
                                                        	hqv->output);
							break;
						}
					}
                        } else if (!continueDecoding)
                                break;

                        // New Start Time
                        st = time(0);

                        // Counter
                        if (hqv->record)
                                hqv->count++;
                }

                // Write out data
                if (data_size > 0) {
                        // HQV Mode
                        if (hqv->do_hqv && hqv->record && !hqv->do_dvd && hqvFile) {
                                ret = fwrite(buffer+splice_pos, 1, data_size-splice_pos, hqvFile);
                                if (ret > 0)
                                        bytes_written_tf += ret;
                                if (ret != (data_size-splice_pos))
                                        av_log(NULL, AV_LOG_ERROR, "\rHQV File Write Error %d bytes of %d bytes written\n", ret, (data_size-splice_pos));
                        }

                        // DQV Mode?
                        if (hqv->do_dqv || hqv->do_dvd) {
                                if (av_fifo_size(hqv->fifo) >= INPUT_FIFO_SIZE) {
                                        av_log(NULL, AV_LOG_FATAL, "\r HQV fifo currently full with %d bytes\n", av_fifo_size(hqv->fifo));
					break; // If fifo is full then might as well give up
                                } else {
                                        ret = av_fifo_generic_write(hqv->fifo, (uint8_t *)buffer, data_size, NULL);
                                        if (ret > 0)
                                                bytes_written_tp += ret;
                                        if (ret != data_size)
                                                av_log(NULL, AV_LOG_ERROR, "\rHQV fifo Write Error %d bytes of %d bytes written\n", ret, data_size);
                                }
                        }

                        splice_pos = 0;
                }
        }

        // Close Output
        if (hqvFile) {
                fclose(hqvFile);
        }
        if (!hqv->keep && hqv->archive && (hqvFile || (hqv->do_dvd && hqv->record)))
                unlink(hqv->output_tmp);
        // Close Input
        if (fd > -1)
                close_source(fd);

        // Free Buffer
        if (buffer)
                free(buffer);

        // Fifo was full, signal DQV to exit
        if (av_fifo_size(hqv->fifo) >= INPUT_FIFO_SIZE) {
                av_log(NULL, AV_LOG_ERROR, "\r Fifo full when Exiting HQV Thread, %d bytes\n", av_fifo_size(hqv->fifo));
                continueDecoding = 0;
                hqv->fifo_full = 1;
        }

        hqv->finished = 1;

        av_log(NULL, AV_LOG_WARNING, "\rExiting HQV Thread\n");

        return 0;
}

// Setup DQV Output Filename
char *dqv_get(char *dqout, char *hostname, char *channel, int dev, char *ext, int round_seconds, char *dir)
{
        time_t t;
        struct tm tmbase;

        t = time(0);
        if(localtime_r(&t,&tmbase))
        {
                char base[512];
                memset(base,0,sizeof(base));
                sprintf(base,"%s-%02d%02d%02d-%02d%02d%02d-%d-%s-%d.%s",
                                channel,
                                tmbase.tm_mon + 1, tmbase.tm_mday, ((tmbase.tm_year+1900)%100),
                                tmbase.tm_hour, tmbase.tm_min, round_seconds?0:tmbase.tm_sec,
                                tmbase.tm_wday, hostname, dev, ext
                );
                sprintf(dqout,"%s/%s", dir, base);
        }
        return dqout;
}

// Setup HQV Output Filename
char *hqv_get(char *dqout, char *hostname, char *channel, int dev, char *ext, int round_seconds, char *dir)
{
        time_t t;
        struct tm tmbase;

        t = time(0);
        if(localtime_r(&t,&tmbase))
        {
                char base[512];
                char fdir[512];
                memset(base,0,sizeof(base));
                memset(fdir,0,sizeof(fdir));
                sprintf(base,"%s-%02d%02d%02d-%02d%02d%02d-%d-%s-%d.%s",
                                channel,
                                tmbase.tm_mon + 1, tmbase.tm_mday, ((tmbase.tm_year+1900)%100),
                                tmbase.tm_hour, tmbase.tm_min, round_seconds?0:tmbase.tm_sec,
                                tmbase.tm_wday, hostname, dev, ext
                );
                sprintf(fdir, "%s/%s", dir, channel);
                mkdir(fdir, 0755);
                sprintf(fdir, "%s/%s/%d", dir, channel, (tmbase.tm_year+1900));
                mkdir(fdir, 0755);
                sprintf(fdir, "%s/%s/%d/%02d", dir, channel, (tmbase.tm_year+1900), tmbase.tm_mon+1);
                mkdir(fdir, 0755);
                sprintf(fdir, "%s/%s/%d/%02d/%02d%02d%02d", dir, channel, (tmbase.tm_year+1900), tmbase.tm_mon+1, tmbase.tm_mon+1, tmbase.tm_mday, ((tmbase.tm_year+1900)%100));
                mkdir(fdir, 0755);

                sprintf(dqout,"%s/%s", fdir, base);

        }
        return dqout;
}

// Setup SDC Output Filename
char *sdc_get(char *dqout, char *hostname, char *channel, int dev, char *ext, int round_seconds, char *dir)
{
        time_t t;
        struct tm tmbase;

        t = time(0);
        if(localtime_r(&t,&tmbase))
        {
                int mon, mday, year, hour, min, sec, wday;
                char base[512];
                char fdir[512];
                memset(base,0,sizeof(base));
                memset(fdir,0,sizeof(fdir));

                mon = tmbase.tm_mon + 1;
                mday = tmbase.tm_mday;
                year = ((tmbase.tm_year+1900)%100);
                hour = tmbase.tm_hour;
                min = tmbase.tm_min;
                sec = tmbase.tm_sec;
                wday = tmbase.tm_wday;

                /* Set min and sec to 0 */
                min = 0;
                sec = 0;

                /* Check for month, year changes */
                if (mon == 12 && mday == 31 && hour == 23) {
                        year++;
                        mon = 1;
                        mday = 1;
                        hour = 0;
                } else if ((mon == 1 || mon == 3 || mon == 5 ||
                                mon == 7 || mon == 8 || mon == 10 ||
                                mon == 12) && mday == 31 && hour == 23)
                {
                        mon++;
                        mday = 1;
                        hour = 0;
                } else if ((mon == 4 || mon == 6 ||
                                mon == 9 || mon == 11) && mday == 30 && hour == 23)
                {
                        mon++;
                        mday = 1;
                        hour = 0;
                } else if (mon == 2 && (mday == 29 || mday == 28) && hour == 23) {
                        if (((tmbase.tm_year+1900) % 4) == 0 &&
                                (((tmbase.tm_year+1900) % 100)||((tmbase.tm_year+1900) % 400) == 0))
                        {
                                /* Leap Year */
                                if (mday == 29) {
                                        mon++;
                                        mday = 1;
                                }
                        } else {
                                if (mday == 28) {
                                        mon++;
                                        mday = 1;
                                }
                        }
                        hour = 0;
                } else {
                        /* Round up */
                        if (hour <= 22)
                                hour++;
                        else {
                                hour = 0;
                                mday++;
                        }
                }

                memset(fdir,0,sizeof(fdir));
                memset(base,0,sizeof(base));

                sprintf(fdir, "%s/%s/%04d/%02d/%02d",
                        dir, channel, year+2000, mon, mday);

                sprintf(fdir, "%s/%s", dir, channel);
                mkdir(fdir, 0755);
                sprintf(fdir, "%s/%s/%d", dir, channel, year+2000);
                mkdir(fdir, 0755);
                sprintf(fdir, "%s/%s/%d/%02d", dir, channel, (tmbase.tm_year+1900), mon);
                mkdir(fdir, 0755);
                sprintf(fdir, "%s/%s/%d/%02d/%02d", dir, channel, (tmbase.tm_year+1900), tmbase.tm_mon+1, mday);
                mkdir(fdir, 0755);

                sprintf(base,"%s-%02d%02d%02d-%02d%02d%02d-%d.%s",
                        channel,
                        mon, mday, year,
                        hour, min, sec,
                        dev, ext);

                sprintf(dqout,"%s/%s", fdir, base);
        }
        return dqout;
}


#include <libavutil/log.h>
#include <libavutil/fifo.h>

#include "ngtc.h"
#include "hqv.h"
#include "mpeg.h"

/******************************************
 * Splicing
 ******************************************/
void splice_init(struct hqv_data *hqv)
{
        hqv->last_stream = 0x00000000;
        hqv->try_count = 0;
        hqv->loops = 0;
        hqv->nxt_buf_start_pos = 0;
}

int splice_mpeg2(struct hqv_data *hqv, unsigned char *buf, int len, int aud_break, int do_splice)
{
        char *f = "\rSPLICE";
        unsigned char *bufptr = buf;
        unsigned int state = 0xFFFFFFFF, v = 0;
        int count = 0; // Position in Buffer
        int lastvid = -1; // Video Stream Start
        int lastaud = -1; // Audio Stream Start
        int lastpad = -1; // Padding Packet
        int lastpack = -1; // Pack Start
        int lastsyshdr = -1; // System Header
        int lastseqstart = -1; // Sequence Start
        unsigned int buf_last_stream = 0x00000000;
        unsigned int pkt_len = 0;
        unsigned int hdr_len = 0;
        unsigned int nxt_pos = 0;
        unsigned int gop_end = 0;
        static char answer[128];
        int drop = 0;
        int hour = 0;
        int min = 0;
        int sec = 0;
        int pictures = 0;
        int closed = 0;
        int broken = 0;
        int splice_at = 0;

        if (aud_break == 1)
                splice_at = AUD_START;
        else if (aud_break == 2)
                splice_at = VID_START;

        for (count = hqv->nxt_buf_start_pos; bufptr < (buf + len); count++,hqv->loops++)
        {
                if (hqv->nxt_buf_start_pos) {
                        av_log(NULL, AV_LOG_DEBUG, "%s: [%d] start_pos[%d]\n", f, count,hqv->nxt_buf_start_pos);
                        bufptr+=hqv->nxt_buf_start_pos;
                        v = *bufptr++;
                        hqv->nxt_buf_start_pos = 0;
                } else
                        v = *bufptr++;
                hqv->nxt_buf_start_pos = 0;

                if (state == 0x000001)
                {
                        int last_packet = 0;
                        state = ((state << 8) | v) & 0xFFFFFF;

                        pkt_len = 0;
                        hdr_len = 0;
                        nxt_pos = 0;

                        if (state >= 0x000001BB && state <= 0x000001FF) {
                                if ((state >= 0x000001BB && state <= 0x000001BC) ||
                                        (state >= 0x000001BE && state <= 0x000001BF) ||
                                        (state >= 0x000001F0 && state <= 0x000001F2) ||
                                        (state == 0x000001F8) || (state >= 0x000001FA))
                                {
                                        // Pack Simple
                                        if ((count+1+2) >= len) {
                                                last_packet = 1;
                                                goto skip_pkt_len;
                                        }
                                } else {
                                        // Pack Complex
                                        if ((count+1+5) < len)
                                                hdr_len = 5 + buf[count+1+4];
                                        else {
                                                last_packet = 1;
                                                goto skip_pkt_len;
                                        }

                                        if ((count+hdr_len) >= len) {
                                                last_packet = 1;
                                                goto skip_pkt_len;
                                        }

                                }
                                pkt_len = buf[count+2] | (buf[count+1] << 8);
                                if (pkt_len > 0) {
                                        pkt_len += 2;
                                        nxt_pos = count+1+pkt_len;
                                } else
                                        pkt_len = 0;

                                if (nxt_pos >= len) {
                                        last_packet = 1;
                                }

                                av_log(NULL, AV_LOG_DEBUG, "%s: - 0x%08X at %d pkt_len %d hdr_len %d nxt_pos [%d] last[%d]\n",
                                                        f,state, count,pkt_len,hdr_len, nxt_pos, last_packet);
                        }
                skip_pkt_len:

                        if (state >= SLICE_MIN && state <= SLICE_MAX) {
                                av_log(NULL, AV_LOG_DEBUG, "%s: Slice Found 0x%08X at %d\n",f,state, count);
                                continue;
                        }

                        if (state >= VID_START && state <= VID_END)
                        {
                                // Video Stream Packet
                                av_log(NULL, AV_LOG_DEBUG, "%s:  Video Stream 0x%04X pos[%d] %d bytes (%d loops)\n",
                                                        f,state, count, pkt_len, hqv->loops);

                                // Video Packet Position
                                buf_last_stream = VID_START;
                                lastvid = count - 3;

				if (lastpack == -1) {
                                        lastsyshdr = -1; // Make sure stray GOP doesn't register without System Header
                                        lastseqstart = -1;
                                        gop_end = 0;
				}

                                // End of Buffer?
                                if (lastsyshdr == -1 && pkt_len > 0) {
                                        if (!last_packet) {
                                                //count += pkt_len;
                                                //bufptr += pkt_len;
                                                //state = 0xFFFFFFFF;
                                        } else {
                                                if (pkt_len && nxt_pos && (nxt_pos-3) > len) {
                                                        hqv->nxt_buf_start_pos = ((nxt_pos-3) - len);
                                                }
                                                goto failed_to_find_gop;
                                        }
                                } else if (pkt_len > 0 && !last_packet) {
                                        gop_end = pkt_len;
                                }
                                continue;
                        } else if (state >= AUD_START && state <= AUD_END) {
                                // Audio Stream Packet
                                av_log(NULL, AV_LOG_DEBUG, "%s:  Audio Stream 0x%04X pos[%d] %d bytes (%d loops)\n",
                                                        f,state, count, pkt_len, hqv->loops);

                                // Audio Packet Position
                                buf_last_stream = AUD_START;
                                lastaud = count - 3;

				if (lastpack == -1) {
                                        lastsyshdr = -1; // Make sure stray GOP doesn't register without System Header
                                        lastseqstart = -1;
                                        gop_end = 0;
				}

                                // End of Buffer?
                                if (pkt_len > 0) {
                                        hqv->audio_frames += (pkt_len - 14);
                                        av_log(NULL, AV_LOG_DEBUG, "%d Audio frames %d %d %d\n", count, hqv->audio_frames, pkt_len-14, hqv->audio_frames%1152);
                                        if (!last_packet) {
                                                count += pkt_len;
                                                bufptr += pkt_len;
                                                state = 0xFFFFFFFF;
                                        } else {
                                                if (pkt_len && nxt_pos && (nxt_pos-3) > len) {
                                                        hqv->nxt_buf_start_pos = ((nxt_pos-3) - len);
                                                }
                                                goto failed_to_find_gop;
                                        }
                                }
                                continue;
                        } else if (state ==  PADDING) {
                                av_log(NULL, AV_LOG_DEBUG, "%s: Padding Stream 0x%04X pos[%d] %d bytes (%d loops)\n",
                                                        f,state, count, pkt_len, hqv->loops);

                                // Padding Position
                                lastpad = count - 3;

                                // End of Buffer?
                                if (pkt_len > 0) {
                                        if (!last_packet) {
                                                count += pkt_len;
                                                bufptr += pkt_len;
                                                state = 0xFFFFFFFF;
                                        } else {
                                                if (pkt_len && nxt_pos && (nxt_pos-3) > len) {
                                                        hqv->nxt_buf_start_pos = ((nxt_pos-3) - len);
                                                }
                                                goto failed_to_find_gop;
                                        }
                                }
                                continue;
                        } else if (state == SYSTEM_HDR) {
                                av_log(NULL, AV_LOG_DEBUG, "%s:  System Header 0x%04X pos[%d] %d bytes (%d loops)\n",
                                                        f,state, count, pkt_len, hqv->loops);

                                // System Header Position
                                lastsyshdr = count - 3;

                                // End of Buffer?
                                if (pkt_len > 0) {
                                        if (!last_packet) {
                                                count += pkt_len;
                                                bufptr += pkt_len;
                                                state = 0xFFFFFFFF;
                                        } else {
                                                if (pkt_len && nxt_pos && (nxt_pos-3) > len) {
                                                        hqv->nxt_buf_start_pos = ((nxt_pos-3) - len);
                                                }
                                                goto failed_to_find_gop;
                                        }
                                }
                                continue;
                        }

                        switch (state)
                        {
                                case SEQ_START:
                                {
                                        av_log(NULL, AV_LOG_DEBUG,
                                                "%s:   Sequence Extension Start 0x%04X pos[%d] (%d loops)\n",f,state, count, hqv->loops);

                                        // Sequence Extension Position
                                        lastseqstart = count - 3;

                                        break;
                                }
                                case SEQ_END:
                                {
                                        /* End of MPEG File */
                                        av_log(NULL, AV_LOG_DEBUG,
                                                "%s:   Sequence Extension End 0x%04X pos[%d] (%d loops)\n",f,state, count, hqv->loops);
                                        //
                                        // We never should end, continuous MPEG file from hardware
                                        //
                                        //if(count >= 3)
                                        //  return count - 3;
                                        break;
                                }
                                case GOP_START:
                                {
                                        if (count+4 < len) {
                                                // GOP Header time/pic/close/broken flags
                                                drop = ((buf[count+1] & 0x80) > 0);
                                                hour = ((buf[count+1] & 0x7C) >> 2);
                                                min = ((buf[count+1] & 0x3) << 4) | ((buf[count+2] & 0xF0) >> 4);
                                                sec = ((buf[count+2] & 0x7) << 3) | ((buf[count+3] & 0xE0) >> 5);
                                                pictures =
                                                        ((buf[count+3] & 0x1F) << 1) | ((buf[count+4] & 0x80) >> 7);
                                                closed = ((buf[count+4] & 0x40) > 0);
                                                broken = ((buf[count+4] & 0x20) > 0);

                                                sprintf(answer,
                                                        "%02d:%02d:%02d.%02d%s%s%s",
                                                        hour, min, sec, pictures,
                                                        drop ? " drop" : "",
                                                        closed ? " closed" : " open",
                                                        broken ? " broken" : "");
                                        } else
                                                sprintf(answer, "%s", "");

                                        av_log(NULL, AV_LOG_DEBUG,
                                                "%s:   GOP Start 0x%04X pos[%d] (%s) (%d loops)\n",f,state, count,answer,hqv->loops);

                                        /* GOP Start */
                                        if (/*((hqv->audio_frames)%1152 == 0) &&*/ (!splice_at || hqv->last_stream == splice_at)
                                                && closed && /*!drop &&*/ !broken /*&& lastpack > -1*/ /*&& lastsyshdr > -1*/ && lastvid > -1 && lastseqstart > -1) {
                                                        // Only if want to splice
                                                        if (do_splice) {
                                                                // Only if contains [Pack Start + System Header + Video Stream + Sequence Header]
                                                                //
                                                                int total_count = (hqv->try_count*len);
                                                                hqv->try_count++;
                                                                total_count += count;

                                                                av_log(NULL, AV_LOG_WARNING,
                                                                        "%s: Found Gop Start (%s) pos %d splice_pos %d audiofrm %d after [%d] trys %d bytes (%d loops)\n",
                                                                                f,answer,count,lastpack,hqv->audio_frames,hqv->try_count,total_count,hqv->loops);

                                                                // Return last pack start position to splice at in our buffer
								if (lastpack != -1)
                                                                	return lastpack;
								else
									return lastvid;
                                                        } else {
                                                                if (gop_end) {
                                                                        int incr = (gop_end - (count - (lastvid+3)));
                                                                        count += incr;
                                                                        bufptr += incr;
                                                                        state = 0xFFFFFFFF;
                                                                        gop_end = 0;
                                                                }
                                                                lastpack = lastsyshdr = lastvid = lastseqstart = -1;
                                                                buf_last_stream = hqv->last_stream = 0x00000000;
                                                        }
                                        } else {
                                                if (do_splice) {
                                                        av_log(NULL, AV_LOG_VERBOSE,
                                                                "%s: INVALID GOP (%s) at %d last_stream[0x%08X] (%d loops)\n",
                                                                        f,answer,count,hqv->last_stream, hqv->loops);
                                                        av_log(NULL, AV_LOG_VERBOSE,
                                                                "%s: == lastpack[%d] lastsyshdr[%d] lastvid[%d] lastseqstart[%d] gop_end[%d]\n",
                                                                        f,lastpack,lastsyshdr,lastvid,lastseqstart, gop_end);
                                                }
                                                if (gop_end && lastvid > -1) {
                                                        int incr = (gop_end - (count - (lastvid+3)));
                                                        count += incr;
                                                        bufptr += incr;
                                                        state = 0xFFFFFFFF;
                                                        gop_end = 0;
                                                }
                                                lastpack = lastsyshdr = lastvid = lastseqstart = -1;
                                                buf_last_stream = hqv->last_stream = 0x00000000;
                                        }
                                        break;
                                }
                                case PICTURE_START:
                                {
                                        av_log(NULL, AV_LOG_DEBUG, "%s:   Picture Start 0x%04X pos[%d] (%d loops)\n",f,state, count, hqv->loops);
                                        break;
                                }
                                case MPEG_HDR:
                                {
                                        av_log(NULL, AV_LOG_DEBUG, "%s: Pack Start 0x%04X pos[%d] (%d loops)\n",f,state, count, hqv->loops);

                                        // Pack Start Position
                                        lastpack = count - 3;
                                        lastsyshdr = -1; // Make sure stray GOP doesn't register without System Header
                                        lastseqstart = -1;
                                        lastvid = -1;
                                        gop_end = 0;

                                        // Save last Video/Audio Stream at Pack Start
                                        if (buf_last_stream > 0x00000000)
                                                hqv->last_stream = buf_last_stream;

                                        // Skip if we can
                                        if ((count + 13) <= len /*&& buf[count+12] == 0x01*/) {
                                                av_log(NULL, AV_LOG_DEBUG, "%s: - jump pack start, from pos[%d] to pos[%d]\n",f,count,(count+9));
                                                count += 9;
                                                bufptr += 9;
                                                state = 0xFFFFFFFF;
                                        }

                                        break;
                                }
                                default:
                                        break;
                        }
                        continue;
                }
                state = ((state << 8) | v) & 0xFFFFFF;
        }
failed_to_find_gop:
	if (do_splice) {
        	hqv->try_count++;
        	av_log(NULL, AV_LOG_VERBOSE, "%s: No GOP Header [%d] trys (%d loops) cur_pos[%d] nxt_pos[%d] nxt_buf_offset[%d]\n",
                        f,hqv->try_count,hqv->loops, count, nxt_pos, hqv->nxt_buf_start_pos);
        	return -1;
	} else
		return 0;
}



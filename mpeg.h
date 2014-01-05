#ifndef MPEG_H
#define MPEG_H

#define MPEG_HDR      0x000001ba
#define SYSTEM_HDR    0x000001bb
#define PADDING       0x000001be
#define VID_START     0x000001e0
#define VID_END       0x000001ef
#define AUD_START     0x000001c0
#define AUD_END       0x000001df
#define SEQ_START     0x000001b3
#define SEQ_END       0x000001b7
#define GOP_START     0x000001b8
#define PICTURE_START 0x00000100
#define SLICE_MIN     0x00000101
#define SLICE_MAX     0x000001af

void splice_init(struct hqv_data *hqv);
int splice_mpeg2(struct hqv_data *hqv, unsigned char *buf, int len, int aud_break, int do_splice);

#endif

#ifndef ENCODER_H
#define ENCODER_H

int init_muxer(CodecSettings *cs, char *o_filename);
int start_muxer(CodecSettings *cs, char *o_filename);
void mux_frame(CodecSettings *cs, uint8_t *outbuf, int size, int type, int rescale, int interleave);
void stop_muxer(CodecSettings *cs);
int init_video_encoder(CodecSettings *cs);
int open_video_encoder(CodecSettings *cs);
int init_audio_encoder(CodecSettings *cs);
int open_audio_encoder(CodecSettings *cs);
int flush_video_encoder(CodecSettings *cs, uint8_t *video_outbuf, int video_outbuf_size, FILE *vFile);
int flush_audio_encoder(CodecSettings *cs, AVFifoBuffer *audio_fifo, uint8_t *audio_outbuf, uint8_t * bit_buffer, int bit_buffer_size, FILE *aFile, int do_padding);

#endif

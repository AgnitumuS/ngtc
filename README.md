# Usage examples:
#
#
# Audio M4A
ngtc -i input.mp3 -vn -c /codecs/hqv -o output.m4a
# Audio/Video MP4
ngtc -i input.mpg -c /codecs/dqv -o output.mp4
# Audio/Video WMV
ngtc -i input.mpg -c /codecs/dqvwmv -o output.wmv

# Raw PCM/YUV files (deinterlaced)
ngtc -i test.mpg -a test.pcm -v test.yuv -c /codecs/dqv -Ol

# Input Raw YUV into M4V 
ngtc -If rawvideo -Iv rawvideo -Iw 320 -Ih 240 -In 15 -Id 1 -i test.yuv -an -c /codecs/dqv -o test.m4v
# Input Raw PCM into M4A
ngtc -If s16le -Ia pcm_s16le -Ir 32000 -Ic 1 -i test.pcm -vn -c /codecs/dqv -o test.m4a
# Input Raw YUV HD 1080p to MP4
ngtc -Iv rawvideo -Iw 1920 -Ih 1080 -In 30000 -Id 1001 -i raw1080p.yuv -an -o test.mp4 -c /codecs/hd1080p -Of 29.97

# V4L Capture to DQV/HQV MP4
ngtc -If video4linux2 -i /dev/video0 -c /codecs/dqv -o dummy.mp4 -Qn 1 -Qc TESTCAPTURE -H -D -n -5 -d -1
# V4L SDC 
ngtc -If video4linux2 -i /dev/video0 -c /codecs/dqv -Qn 0 -Qc TEST00 -Qo 2 -l 3600 -H -n -5 -d -1
# V4L HQV Only
ngtc -If video4linux2 -i /dev/video0 -c /codecs/dqv -Qn 0 -Qc TEST00 -H -n -5 -d -1
# V4L DQV/HQV WMV Extended Mode
ngtc -If video4linux2 -i /dev/video0 -c /codecs/dqvwmv -o dummy.wmv -Qn 0 -Qc TEST00 -Qe 1 -H -D -n -5 -d -1

# Playback raw video/audio
mplayer -demuxer rawvideo -rawvideo w=320:h=240:fps=15:i420 test.yuv
mplayer -demuxer rawaudio -rawaudio channels=1:rate=32000 test.pcm

# Mux raw aac/h264 streams
MP4Box -inter 1000 -fps 15 -add input.264#video -add input.aac#audio -new output.mp4

# Process directory of media into mp4 and thumbnail files
process.pl -ie mpg -s /data/in -e mp4 -f /data/out -t /data/thumbs -v

# Process directory of media into m4a audio files
process.pl -ie mpg -s /data/in -e mp4 -f /data/out -v -ao


# Watermarks
ffmpeg -i /u2/video/Compat_Test/source/KNBC-072710-185632-190000.mpg -vf "movie=0:png:/home/ckennedy/watermark.png [logo];[in][logo] overlay=main_w-overlay_w-5:main_h-overlay_h-5 [out]" -target ntsc-dvd -b 3000000 -maxrate 3250000 -acodec copy -y test.mpg

ngtc -i /u2/video/Compat_Test/source/KNBC-072710-185632-190000.mpg --ptsgen --vf "movie=0:png:/home/ckennedy/watermark.png [logo];[in][logo] overlay=main_w-overlay_w-5:main_h-overlay_h-5 [out]" -c /codecs/dqv -o test.mp4



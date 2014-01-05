#include <libavutil/log.h>
#include "io.h"

#define DEBUG_OUTPUT_FILE "/data/debug"
#define DEBUG_ON (0)
FILE *dfile;

/***************************************************************
 * INPUT FILE HANDLING
 **************************************************************/
int open_source(char *file) {
        int fd;
        fd = open(file, O_RDWR);
        if (fd < 0) {
                av_log(NULL, AV_LOG_FATAL, "Error: open source file %s failed.\n",
                                file);
        }
	if (DEBUG_ON) {
		char debug_file[255];
		char inum = file[strlen(file)-1];

		sprintf(debug_file, "%s_%c.mpg", DEBUG_OUTPUT_FILE, inum);

                av_log(NULL, AV_LOG_WARNING, "Opening debug output raw mpeg file %s.\n",
                                debug_file);

		dfile = fopen(debug_file, "w+");
	}
        return fd;
}

void close_source(int fd) {
        if (fd > -1)
                close(fd);
        else
                av_log(NULL, AV_LOG_FATAL, "Error, source file handle is invalid %d.\n",
                                fd);
	if (DEBUG_ON)
		fclose(dfile);
}

int read_source(int fd, unsigned char *buffer, int size) {
        struct pollfd pfds[1];
        int rk, pos, ret;

        if (!buffer)
                return 0;

        pos = ret = 0;

        while (pos < size) {
                pfds[0].fd = fd;
                pfds[0].events = POLLIN | POLLPRI;

                rk = size - pos;

                if ((ret = poll(pfds, 1, 500)) <= 0) {
                        if (ret < 0) {
                                av_log(NULL, AV_LOG_ERROR,
                                        "HQV read failed with errno %d when reading %d bytes\n",
                                        errno, size-pos);
                                return ret;
                        } else
                                break;
                }

                rk = read(fd, &buffer[pos], rk);
                if (rk > 0)
                        pos += rk;
        }

        if (!pos)
                av_log(NULL, AV_LOG_INFO, "HQV read only got %d bytes\n", pos);

	if (DEBUG_ON && pos > 0)
		if (fwrite(buffer, 1, pos, dfile) != pos)
			av_log(NULL, AV_LOG_ERROR, "Error: Debug output couldn't write %d bytes", pos);

        return pos;
}


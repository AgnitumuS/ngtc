#ifndef IO_H
#define IO_H

#include <poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

int open_source(char *file);
void close_source(int fd);
int read_source(int fd, unsigned char *buffer, int size);

#endif

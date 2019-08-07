#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>

enum { MAX_MSG_SIZE = 64*1024 };
enum { SYNC = 1000, ASYNC };
char buf[MAX_MSG_SIZE + 1];

int main(int argc, char** argv)
{
	if (argc == 1 || argc > 3) {
		puts("usage: pop | push $msg | sync $nr | async $nr");
		return 0;
	}

	int fd = open("/proc/queue_test", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "module does not load\n");
		return 1;
	}

	if (!strcmp(argv[1], "pop")) {
		if (read(fd, buf, MAX_MSG_SIZE) > 0)
			puts(buf);
	} else if (!strcmp(argv[1], "push")) {
		write(fd, argv[2], strlen(argv[2]) + 1);
	} else if (!strcmp(argv[1], "sync")) {
		ioctl(fd, SYNC, atoi(argv[2]));
	} else if (!strcmp(argv[1], "async")) {
		ioctl(fd, ASYNC, atoi(argv[2]));
	}

	close(fd);

	return 0;
}

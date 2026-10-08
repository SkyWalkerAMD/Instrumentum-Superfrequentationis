#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static unsigned char *read_module(const char *path, size_t *length)
{
	struct stat st;
	unsigned char *buf;
	size_t done = 0;
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return NULL;
	if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
	    (unsigned long long)st.st_size > 64ULL * 1024 * 1024) {
		errno = EINVAL;
		close(fd);
		return NULL;
	}
	buf = malloc((size_t)st.st_size);
	if (!buf) {
		close(fd);
		return NULL;
	}
	while (done < (size_t)st.st_size) {
		ssize_t n = read(fd, buf + done, (size_t)st.st_size - done);
		if (n <= 0) {
			int saved = n < 0 ? errno : EIO;
			free(buf);
			close(fd);
			errno = saved;
			return NULL;
		}
		done += (size_t)n;
	}
	close(fd);
	*length = done;
	return buf;
}

int main(int argc, char **argv)
{
	unsigned char *image;
	size_t length = 0;
	int second_errno = 0;
	long second_rc;

	if (argc != 2) {
		fprintf(stderr, "usage: %s MODULE.ko\n", argv[0]);
		return 2;
	}
	image = read_module(argv[1], &length);
	if (!image) {
		perror("read module image");
		return 2;
	}

	second_rc = syscall(SYS_init_module, image, length, "");
	if (second_rc < 0)
		second_errno = errno;
	printf("{\"second_syscall\":\"init_module\",\"second_rc\":%ld,"
	       "\"second_errno\":%d}\n", second_rc, second_errno);
	free(image);
	return second_rc == -1 && second_errno == EEXIST ? 0 : 1;
}

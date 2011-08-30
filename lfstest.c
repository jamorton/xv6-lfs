#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

#define NFILE 40
#define BUFLEN 20000

int
main(int argc, char *argv[])
{
	int st = uptime(), k = 0, i = 0;

	char * buf = malloc(BUFLEN);

	for (k = 0; k < NFILE; k++) {
		printf(1, "[%d%%] file %d\n", (int)((float)k / NFILE * 100), k);
		char path[] = "lfstest0";
		path[7] += k;
		int fd = open(path, O_CREATE | O_RDWR);
		for (i = 0; i < (20000 / BUFLEN); i++)
			if (write(fd, buf, BUFLEN) != BUFLEN) {
				printf(1, "fail");
				exit();
			}
		close(fd);
	}

	int en = uptime();

	printf(1, "time: %d\n", en - st);
	exit();
}

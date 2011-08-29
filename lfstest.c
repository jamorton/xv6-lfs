#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

int
main(int argc, char *argv[])
{
	int fd = open("lfstest0", O_CREATE | O_RDWR);

	int st = uptime(), i = 0;
	for (i = 0; i < 100; i++)
		printf(fd, "0");
	int en = uptime();

	close(fd);
	printf(1, "time: %d\n", en - st);

	exit();
}

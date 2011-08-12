/*
  mkfs for a log-structured filesystem

  todo:
  - support inodes bigger than a segment
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/param.h> // for min/max
#include <stdint.h>

#include "types.h"
#include "fs.h"

#define DRIVE_SIZE (SEGSIZE * 20) // 20 segments
#define MAX_INODES 1024

// global variables
int fsd;
struct disk_superblock sb;

block_t imap[MAX_INODES];
uint imap_cur;

// block funcs
block_t balloc(void);
void bwrite(block_t, const void *);

// inode funcs
struct disk_inode * ialloc(short);
void iappend(struct disk_inode *, void *, uint);
block_t write_indirect(uint, void *, uint, uint);
block_t append_block(block_t *, void * data, uint off, uint len);

int main(int argc, char * argv[])
{
//	if (argc < 2) {
//		printf("Usage: mkfs fs.img files...\n");
//		exit(1);
//	}

	fsd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
	if (fsd < 0) {
		perror(argv[1]);
		exit(1);
	}
	
	block_t sb_block = balloc();
	(void)sb_block;

	block_t addrs[NADDRS];
	bzero(addrs, sizeof(addrs));
	append_block(addrs, NULL, BSIZE * 2000 + 50, 200);
	
	return 0;
}

block_t balloc(void)
{
	static block_t bcur = 0;

	uchar zeroes[BSIZE];
	bzero(zeroes, BSIZE);

	block_t b = bcur++;
	bwrite(b, zeroes);
	return b;
}

#define FLOC(a) ((a) * BSIZE)

void bread(block_t addr, void * buf)
{
	assert(lseek(fsd, FLOC(addr), SEEK_SET) == FLOC(addr));
	assert(read(fsd, buf, BSIZE) == BSIZE);
}

void bwrite(block_t addr, const void * data)
{
	assert(lseek(fsd, FLOC(addr), SEEK_SET) == FLOC(addr));
	assert(write(fsd, data, BSIZE)  == BSIZE);
}

struct disk_inode * ialloc(short type)
{
	struct disk_inode * ip = malloc(sizeof(struct disk_inode));
	ip->type = type;
	ip->nlink = 1;
	ip->size = 0;
	return ip;
}

block_t read_addrblock(block_t * addr, block_t ** ret)
{
	if (*addr == 0)
		*addr = balloc();
	block_t r = *addr;
	bread(*addr, *ret);
	return r;
}

void inode_addr(block_t * addrs, block_t ** ret,  uint level)
{
	if (level == 0) {
		*ret = addrs;
		return;
	}
	uint a = level + NDIRECT - 1;
	read_addrblock(addrs + a, ret);
}

block_t append_block(block_t * addrs, void * data, uint off, uint len)
{
	printf("input %u\n", off / BSIZE);
	uint bn = off / BSIZE, cnt = 0;
	int level = -1;
	while (1) {
		cnt += INDIRECT_SIZE(++level);
		if (cnt > bn)
			break;
		off -= INDIRECT_SIZE(level) * BSIZE;
	}

	printf("level: %u  new off: %u\n", level, off / BSIZE);

	block_t * level_addrs = malloc(NINDIRECT * sizeof(block_t));
	inode_addr(addrs, &level_addrs, level);

	uint l;
	block_t block_n;
	for (l = level; l > 0; l--) {
		uint c, div = BSIZE;
		for (c = l - 1; c > 0; c--)
			div *= NINDIRECT;
		uint num = off / div;
		off = off % div;
		printf(" div %u   bn: %u, new off: %u\n", div/BSIZE, num, off/BSIZE);
		block_n = read_addrblock(level_addrs + num, &level_addrs);
	}

	free(level_addrs);

	assert(off + len < BSIZE);

	char * out = malloc(BSIZE);
	bread(block_n, out);
	memcpy(out, data + off, len);
	bwrite(block_n, out);
	free(out);
	
	return 0;
}


void iappend(struct disk_inode * ip, void * data, uint len)
{
	uint wr = 0;
}

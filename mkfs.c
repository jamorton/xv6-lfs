/*
  mkfs for a log-structured filesystem
  see README.
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
#define stat xv6_stat  // avoid clash with host struct stat
#include "stat.h"

#define DRIVE_SIZE (SEGSIZE * 20) // 20 segments
#define MAX_INODES (BSIZE / sizeof(block_t))

// global variables
int fsd;
struct disk_superblock sb;

block_t imap[MAX_INODES];
static block_t cur_block = 1; // 0 reserved for superblock
static inode_t cur_inode = 0;
static uint seg_nblocks = 0;

// block funcs
block_t balloc(void);
void bwrite(block_t, const void *);

// inode funcs
inode_t ialloc(short);
void iread(inode_t, struct disk_inode *);
void iwrite(inode_t, struct disk_inode *);
void iappend(inode_t, void *, uint);
block_t append_block(block_t *, void * data, uint off, uint len);

int main(int argc, char * argv[])
{
	bzero(&sb, sizeof(sb));

	if (argc < 2) {
		printf("Usage: mkfs [image file] [input files...]\n");
		exit(1);
	}

	fsd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
	if (fsd < 0) {
		perror(argv[1]);
		exit(1);
	}
	
	inode_t rootino = ialloc(T_DIR);
	
	struct dirent de;
	bzero(&de, sizeof(de));
	de.inum = rootino;
	strcpy(de.name, ".");
	iappend(rootino, &de, sizeof(de));

	bzero(&de, sizeof(de));
	de.inum = rootino;
	strcpy(de.name, "..");
	iappend(rootino, &de, sizeof(de));

	uint i;
	for (i = 2; i < argc; i++)
	{
		int fd;
		if((fd = open(argv[i], 0)) < 0){
			perror(argv[i]);
			exit(1);
		}

		if (argv[i][0] == '_')
			++argv[i];

		inode_t inum = ialloc(T_FILE);

		bzero(&de, sizeof(de));
		de.inum = inum;
		strncpy(de.name, argv[i], DIRSIZ);
		iappend(rootino, &de, sizeof(de));

		char bf[BSIZE];
		int cc;
		while ((cc = read(fd, bf, sizeof(bf))) > 0)
			iappend(inum, bf, cc);

		close(fd);
	}
	
	block_t imap_block = balloc();
	bwrite(imap_block, imap);
	
	sb.imap = imap_block;
	sb.nblocks = cur_block;
	sb.ninodes = cur_inode;
	bwrite(0, &sb);

	close(fsd);

	return 0;
}

block_t balloc(void)
{
	char zeroes[BSIZE];
	bzero(zeroes, BSIZE);

	bwrite(cur_block, zeroes);
	seg_nblocks++;

	// segment is full.
	if (seg_nblocks  >= SEGBLOCKS) {
		seg_nblocks = 0;
		block_t segstart = cur_block - SEGBLOCKS;
		sb.segment = segstart;
		sb.nsegs++;
	}

	return cur_block++;
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

inode_t ialloc(short type)
{
	if (cur_inode >= MAX_INODES) {
		printf("Error: inode limit exceeded.\n");
		exit(1);
	}
	struct disk_inode * ip = malloc(BSIZE);
	ip->type = type;
	ip->nlink = 1;
	ip->size = 0;

	block_t nb = balloc();

	bwrite(nb, ip);
	imap[cur_inode] = nb;
	free(ip);

	return cur_inode++;
}

void iwrite(inode_t i, struct disk_inode * di)
{
	char buf[BSIZE];
	bzero(buf, BSIZE);
	*((struct disk_inode *)buf) = *di;
	bwrite(imap[i], buf);
}

void iread(inode_t i, struct disk_inode * di)
{
	char buf[BSIZE];
	bzero(buf, BSIZE);
	bread(imap[i], buf);
	*di = *((struct disk_inode *)buf);
}

block_t append_block(block_t * addrs, void * data, uint off, uint len)
{
	block_t bnext;

	// find indirection level to start at
	uint bn = off / BSIZE, cnt = 0, level = 0;
	while (1) {
		cnt += INDIRECT_SIZE(level);
		if (cnt > bn)
			break;
		off -= INDIRECT_SIZE(level++) * BSIZE;
	}

	block_t * level_addrs;

	if (level != 0) {
		level_addrs = malloc(BSIZE);
		uint a = level + NDIRECT - 1;
		if (addrs[a] == 0)
			addrs[a] = balloc();
		bnext = addrs[a];
		bread(addrs[a], level_addrs);
	} else {
		bnext = addrs[off / BSIZE];
		off = off % BSIZE;
		goto append_block_final;
	}

	uint l;
	for (l = level; l > 0; l--) {
		uint c = l - 1, div = BSIZE;
		while (c--)
			div *= NINDIRECT;
		block_t n = off / div;
		off = off % div;

		if (level_addrs[n] == 0) {
			level_addrs[n] = balloc();
			bwrite(bnext, level_addrs);
		}
		bnext = level_addrs[n];
		bread(bnext, level_addrs);
	}

	free(level_addrs);

append_block_final:
	assert(off + len <= BSIZE);
	char * out = malloc(BSIZE);
	bread(bnext, out);
	memcpy(out + off, data + off, len);
	bwrite(bnext, out);
	free(out);
	
	return 0;
}

void iappend(inode_t i, void * data, uint len)
{
	struct disk_inode * di = malloc(sizeof(struct disk_inode));
	iread(i, di);

	uint wr = di->size;
	uint max = wr + len;
	while (wr < max) {
		uint st  = MIN(BSIZE - wr % BSIZE, max - wr);
		append_block(di->addrs, data, wr, st);
		wr += st;
	}

	di->size = wr;

	iwrite(i, di);
	free(di);
}

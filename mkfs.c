
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

// global variables
int fsd;
struct disk_superblock sb;

block_t imap[MAX_INODES];
static block_t cur_block = 1 + SEGMETABLOCKS; // 0 for superblock
static inode_t cur_inode = 1; // inode 0 means null
static uint seg_block = 0;

// block funcs
block_t balloc(void);
void bwrite(block_t, const void *);
block_t data_block(block_t *, uint);

// inode funcs
inode_t ialloc(short);
void iread(inode_t, struct disk_inode *);
void iwrite(inode_t, struct disk_inode *);
void iappend(inode_t, void *, uint);

int main(int argc, char * argv[])
{
	bzero(&sb, sizeof(sb));
	sb.nsegs = 0;
	sb.segment = 0;

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
	
	char buf[BSIZE];
	bzero(buf, BSIZE);

	sb.imap = imap_block;
	sb.nblocks = cur_block;
	sb.ninodes = cur_inode;
	memcpy(buf, &sb, sizeof(sb));
	bwrite(0, buf);

	close(fsd);

	return 0;
}

block_t balloc(void)
{
	char zeroes[BSIZE];
	bzero(zeroes, BSIZE);

	block_t bret = cur_block++;
	bwrite(bret, zeroes);

	// segment is full.
	if (++seg_block == SEGDATABLOCKS) {
		seg_block = 0;
		sb.segment = cur_block - SEGBLOCKS;
		sb.nsegs++;

		// segment metadata blocks, write zeroes for now
		uint k;
		for (k = 0; k < SEGMETABLOCKS; k++)
			bwrite(sb.segment + k, zeroes);

		cur_block += SEGMETABLOCKS;
	}

	return bret;
}

// 0-512 is boot sector
#define FLOC(a) ((a) * BSIZE + 512)

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
	struct disk_inode ip;
	bzero(&ip, sizeof(ip));

	ip.type = type;
	ip.nlink = 1;
	ip.size = 0;

	block_t nb = balloc();
	char buf[BSIZE];
	bzero(buf, BSIZE);

	memcpy(buf, &ip, sizeof(ip));
	bwrite(nb, buf);
	imap[cur_inode - 1] = nb;

	return cur_inode++;
}

void iwrite(inode_t i, struct disk_inode * di)
{
	char buf[BSIZE];
	bzero(buf, BSIZE);
	memcpy(buf, di, sizeof(struct disk_inode));
	bwrite(imap[i - 1], buf);
}

void iread(inode_t i, struct disk_inode * di)
{
	char buf[BSIZE];
	bzero(buf, BSIZE);
	bread(imap[i - 1], buf);
	memcpy(di, buf, sizeof(struct disk_inode));
}

block_t data_block(block_t * addrs, uint off)
{
	const uint bn = off / BSIZE;
	uint cnt = 0, level = 0;
	while (1) {
		cnt += INDIRECT_SIZE(level);
		if (cnt > bn)
			break;
		off -= INDIRECT_SIZE(level++) * BSIZE;
	}

	uint addr_off = (level == 0 ? (off / BSIZE) : (level + NDIRECT - 1));

	if (addrs[addr_off] == 0)
		addrs[addr_off] = balloc();

	block_t bnext = addrs[addr_off];
	block_t * level_addrs = malloc(BSIZE);
	
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

	return bnext;
}


void iappend(inode_t i, void * data, uint len)
{
	char out[BSIZE];

	struct disk_inode di;
	iread(i, &di);

	uint wr = di.size;
	uint max = wr + len;
	uint data_off = 0;

	while (wr < max) {
		uint len  = MIN(BSIZE - wr % BSIZE, max - wr);
		block_t db = data_block(di.addrs, wr);

		bread(db, out);
		memcpy(out + wr % BSIZE, data + data_off, len);
		bwrite(db, out);
		
		wr += len;
		data_off += len;
	}

	di.size = max;

	iwrite(i, &di);
}

#ifndef __FS_H__
#define __FS_H__

#define BSIZE 4096
#define SEGSIZE 1024*1024 // 1mb

typedef uint block_t;

struct disk_superblock {
	uint nsegs; // number of segments
	uint segment; // checkpoint
	block_t imap; // start block of imap
	uint ninodes;
	uint blocks;
};

#define DISK_INODE_DATA 12 // size of disk_inode excluding addrs
#if DISK_INODE_DATA % 4 != 0
  #error disk_inode data must be multiple of 12
#endif

#define INDIRECT_LEVELS 2 // direct, indirect, double indirect

#define NADDRS ((64 - DISK_INODE_DATA) / 4)
#define NDIRECT (NADDRS - INDIRECT_LEVELS)
#define NINDIRECT (BSIZE / sizeof(block_t))

static const uint __LEVEL_SIZES[] = {
	NDIRECT,
	NINDIRECT,
	NINDIRECT * NINDIRECT,
	NINDIRECT * NINDIRECT * NINDIRECT
};

#define INDIRECT_SIZE(n) (__LEVEL_SIZES[(n)])

// must change this when INDIRECT_LEVELS is changed
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT * NINDIRECT)

struct disk_inode {
	short type;
	short major;
	short minor;
	short nlink;
	uint size;
	block_t addrs[NADDRS];
};

#define IPB (BSIZE/sizeof(disk_inode));

#endif

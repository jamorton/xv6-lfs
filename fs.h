#ifndef __FS_H__
#define __FS_H__

#define BSIZE (2048)
#define SEGSIZE (1024*512) // 512kb
#define SEGBLOCKS (SEGSIZE/BSIZE)
#define SEGMETABLOCKS (1)
#define SEGDATABLOCKS (SEGBLOCKS-SEGMETABLOCKS)

// sectors per block
#define SPB (BSIZE / 512)
#define IS_BLOCK_SECTOR(a) (((a) & (SPB - 1)) == 0) // is divisible by SPB

// block to sector, sector to block
// block 0 is sector 1, block 1 is sector 9, etc.
// sector 0 is reserved for the bootloader
#define B2S(b) ((b) * SPB + 1)
#define S2B(s) (((s) - 1) / SPB)

typedef uint block_t;
typedef uint inode_t;

struct disk_superblock {
	uint nsegs; // number of segments
	uint segment; // checkpoint
	block_t imap; // imap block
	uint ninodes;
	uint nblocks;
};

#define DISK_INODE_DATA 12 // size of disk_inode excluding addrs
#if DISK_INODE_DATA % 4 != 0
  #error disk_inode data must be multiple of 12
#endif

#define INDIRECT_LEVELS 2 // direct, indirect, double indirect

#define NADDRS ((64 - DISK_INODE_DATA) / 4)
#define NDIRECT (NADDRS - INDIRECT_LEVELS)
#define NINDIRECT (BSIZE / sizeof(block_t))

#define MAX_INODES (BSIZE / sizeof(block_t))

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

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

// disk block structure
struct buf {
  int flags;
  uint dev;
  block_t block;
  struct buf *prev; // LRU cache list
  struct buf *next;
  struct buf *qnext; // disk queue
  uchar data[BSIZE];
};
#define B_BUSY  0x1  // buffer is locked by some process
#define B_VALID 0x2  // buffer has been read from disk
#define B_DIRTY 0x4  // buffer needs to be written to disk

#define ROOTINO 1
#define ROOTDEV 1

#endif

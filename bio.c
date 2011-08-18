// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
// 
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to flush it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.
// 
// The implementation uses three state flags internally:
// * B_BUSY: the block has been returned from bread
//     and has not been passed back to brelse.  
// * B_VALID: the buffer data has been initialized
//     with the associated disk block contents.
// * B_DIRTY: the buffer data has been modified
//     and needs to be written to disk.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "fs.h"

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head;
} bcache;

struct {
  struct spinlock lock;
  block_t start; // where seg will be written
  uint count; // number of blocks already copied into data
  uchar data[SEGSIZE];
} seg;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
  initlock(&seg.lock, "seg");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    b->dev = -1;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }

  seg.start = seg.count = 0;
  memset(seg.data, 0, sizeof(seg.data));
}

// Look through buffer cache for block on device dev.
// If not found, allocate fresh block.
// In either case, return locked buffer.
struct buf*
bget(uint dev, block_t block)
{
  struct buf *b;

  acquire(&bcache.lock);

 loop:
  // Try for cached block.
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->block == block){
      if(!(b->flags & B_BUSY)){
        b->flags |= B_BUSY;
        release(&bcache.lock);
        return b;
      }
      sleep(b, &bcache.lock);
      goto loop;
    }
  }

  // Allocate fresh block.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if((b->flags & B_BUSY) == 0 && (b->flags & B_DIRTY) == 0){
      b->dev = dev;
      b->block = block;
      b->flags = B_BUSY;
      release(&bcache.lock);
      return b;
    }
  }

  panic("bget: no buffers");
}

// Return a B_BUSY buf with the contents of the indicated disk block.
struct buf*
bread(uint dev, block_t block)
{
  struct buf *b;

  b = bget(dev, block);
  if(!(b->flags & B_VALID)) {
    // this simple of a check won't work with segment recycling
    if (block == 0 || seg.start == 0 || block < seg.start)
      iderw(b);
    else {
      acquire(&seg.lock);
      memmove(b->data, seg.data + (block - seg.start) * BSIZE, BSIZE);
      release(&seg.lock);
      b->flags |= B_VALID;
      b->flags &= ~B_DIRTY;
    }
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
// fixed version for writing superblock
void
bwrite_fixed(struct buf *b)
{
  if((b->flags & B_BUSY) == 0)
    panic("bwrite");

  b->flags |= B_DIRTY;
  iderw(b);  
}

block_t
bwrite(void *data)
{
  acquire(&seg.lock);
  
  if (seg.start == 0) {
    struct disk_superblock sb;
    readsb(ROOTDEV, &sb);
    seg.start = sb.next;
  }

  uint off = seg.count + SEGMETABLOCKS;
  memmove(seg.data + off * BSIZE, data, BSIZE);
  block_t bn = seg.start + off;

  if (++seg.count == SEGDATABLOCKS) {
    cprintf("lol %u\n", seg.count);
    uint k;
    // write zeroes for block metadata for now
    for (k = 0; k < SEGMETABLOCKS; k++) {
      struct buf b;
      memset(b.data, 0, BSIZE);
      b.dev = ROOTDEV;
      b.flags = B_DIRTY | B_BUSY;
      iderw(&b);
    }

    for (k = 0; k < SEGDATABLOCKS; k++) {
      struct buf b;
      memmove(b.data, &seg.data[k * BSIZE], BSIZE);
      b.dev = ROOTDEV;
      b.flags = B_DIRTY | B_BUSY;
      iderw(&b);
    }
    
    struct disk_superblock sb;
    readsb(ROOTDEV, &sb);
    sb.segment = sb.next;
    sb.next += SEGBLOCKS;
    sb.nsegs++;
    sb.nblocks += SEGBLOCKS;
    writesb(ROOTDEV, &sb);

    memset(seg.data, 0, sizeof(seg.data));
    seg.count = seg.start = 0;
  }

  release(&seg.lock);

  return bn;
}

// Release the buffer b.
void
brelse(struct buf *b)
{
  if((b->flags & B_BUSY) == 0)
    panic("brelse");

  acquire(&bcache.lock);

  b->next->prev = b->prev;
  b->prev->next = b->next;
  b->next = bcache.head.next;
  b->prev = &bcache.head;
  bcache.head.next->prev = b;
  bcache.head.next = b;

  b->flags &= ~B_BUSY;
  wakeup(b);

  release(&bcache.lock);
}



/*
  acquire(&seg.lock);

  // uninitialized seg

  if ((b->flags & B_DIRTY) != 0)
    return;

  seg.data[seg.count] = b;
  b->flags |= B_DIRTY;
  b->block = seg.start + SEGMETABLOCKS + seg.count++;

  if (seg.count == SEGBLOCKS) { 
    uint k;
    for (k = 0; k < SEGMETABLOCKS; k++) {
      struct buf meta;
      memset(meta.data, 0, BSIZE);
      meta.dev = b->dev;
      meta.flags = B_DIRTY;
      iderw(&meta);
    }

    for (k = 0; k < SEGDATABLOCKS; k++)
      iderw(seg.data[k]);
    
    struct disk_superblock sb;
    readsb(ROOTDEV, &sb);
    sb.segment = sb.next;
    sb.next += SEGBLOCKS;
    sb.nsegs++;
    sb.nblocks += SEGBLOCKS;
    writesb(ROOTDEV, &sb);

    memset(seg.data, 0, sizeof(seg.data));
    seg.count  = seg.start = 0;
  }

  release(&seg.lock);

*/

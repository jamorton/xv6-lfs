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

#define BUFSIZE NBUF + SEGBLOCKS

struct {
  struct spinlock lock;
  struct buf buf[BUFSIZE];
  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head;
} bcache;

struct {
  uchar busy; // is writing?
  struct spinlock lock;
  block_t start; // where seg will be written
  uint count; // number of blocks already copied into data
  struct buf * blocks[SEGDATABLOCKS];
} seg;

static void waitseg(void)
{
  if (seg.busy != 1)
    return;
  cprintf("SLEEPING\n");
  acquire(&seg.lock);
  while (seg.busy == 1)
    sleep(&seg, &seg.lock);
  release(&seg.lock);
  cprintf("WAKING\n");
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
  initlock(&seg.lock, "seg");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+BUFSIZE; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    b->dev = -1;
    b->flags = 0;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }

  seg.start = seg.count = 0;
  memset(seg.blocks, 0, sizeof(seg.blocks));
}

// Return a new, locked buf without an assigned block
struct buf*
balloc(uint dev)
{
  waitseg();
  struct buf * b;
  acquire(&bcache.lock);
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if((b->flags & B_BUSY) == 0 && (b->flags & B_DIRTY) == 0){
      b->dev = dev;
      b->block = 0;
      b->flags = B_BUSY;
      release(&bcache.lock);
      return b;
    }
  }
  panic("balloc: no free buffers");
}

// Look through buffer cache for block on device dev.
// If not found, allocate fresh block.
// In either case, return locked buffer.
struct buf*
bget(uint dev, block_t block)
{
  if (block == 0)
    panic("bget: invalid block");
 
  waitseg();
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
  
  release(&bcache.lock);

  if (seg.start !=0 && block > seg.start && block < seg.start + SEGBLOCKS)
    panic("bget: block in new seg range.");
  
  b = balloc(dev);
  b->block = block;
  return b;
}
// Return a B_BUSY buf with the contents of the indicated disk block.
struct buf*
bread(uint dev, block_t block)
{
  waitseg();
  struct buf *b;
  b = bget(dev, block);

  if(!(b->flags & B_VALID))
    iderw(b);

  return b;
}

block_t
bwrite(struct buf *b)
{
  if ((b->flags & B_BUSY) == 0)
      panic("bwrite");

  // superblock writing.
  if (b->block == 1) {
    b->flags |= B_DIRTY;
    iderw(b);
    return 0;
  }

  waitseg();
  acquire(&seg.lock);
  struct disk_superblock * sb = getsb();

  // initialize new seg
  if (seg.start == 0)
    seg.start = sb->next;

  if ((b->flags & B_DIRTY) != 0) {
    release(&seg.lock);
    return b->block;
  }

  seg.blocks[seg.count] = b;
  b->block = seg.start + SEGMETABLOCKS + seg.count++;
  b->flags |= B_DIRTY;

  release(&seg.lock);

  if (seg.count == SEGDATABLOCKS) {
    cprintf("WRITE SEGMENT\n");
    seg.busy = 1;

    // write zeroes for block metadata for now    
    uint k;
    struct buf meta;
    memset(meta.data, 0, sizeof(meta.data));
    meta.dev = ROOTDEV;
    for (k = 0; k < SEGMETABLOCKS; k++) {
      meta.flags = B_DIRTY | B_BUSY;
      meta.block = seg.start + k;
      iderw(&meta);
    }

    for (k = 0; k < SEGDATABLOCKS; k++) {
      int prevflags = seg.blocks[k]->flags;
      seg.blocks[k]->flags = B_DIRTY | B_BUSY;
      iderw(seg.blocks[k]);
      seg.blocks[k]->flags = prevflags & (~B_DIRTY);
    }
    
    sb->segment = seg.start;
    sb->next += SEGBLOCKS;
    sb->nsegs++;
    sb->nblocks += SEGBLOCKS;

    memset(seg.blocks, 0, sizeof(seg.blocks));
    seg.count = seg.start = seg.busy = 0;
    wakeup(&seg);
  }

  return b->block;
}

// Release the buffer b.
void
brelse(struct buf *b)
{
  // if((b->flags & B_BUSY) == 0)
  //  panic("brelse");

  waitseg();
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

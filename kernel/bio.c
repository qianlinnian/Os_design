// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13 //13个桶
struct bucket {
  struct spinlock lock;  //每个桶独立的锁
  struct buf head;//每个桶独立的LRU链表
}; 
struct bucket bcache[NBUCKET];//13个桶，
struct buf buf[NBUF];  // 独立存储所有buffer

void
binit(void)
{
  // 初始化桶
  for (int i = 0; i < NBUCKET; i++) {
    initlock(&bcache[i].lock, "bcache");
    // 每个桶都有独立的LRU链表
    bcache[i].head.prev = &bcache[i].head;
    bcache[i].head.next = &bcache[i].head;
  }
  for (struct buf *b = buf; b < buf + NBUF; b++) {
    initsleeplock(&b->lock,"buffer");
    int bucket = (b - buf) % NBUCKET; // 分配到对应的桶
    b->next = bcache[bucket].head.next;
    b->prev = &bcache[bucket].head; 
    bcache[bucket].head.next->prev = b;
    bcache[bucket].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  int bucket =blockno % NBUCKET; // 根据blockno选择桶
  acquire(&bcache[bucket].lock); // 获取对应桶的锁
  struct buf *b;
  //第一步：在目标桶中查找
  for(b = bcache[bucket].head.next; b != &bcache[bucket].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache[bucket].lock); // 释放桶的锁
      acquiresleep(&b->lock); // 获取buffer的sleep lock
      return b;
    }
  }

  // 第二步：在目标桶中查找空闲buffer
  for(b = bcache[bucket].head.prev; b != &bcache[bucket].head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache[bucket].lock);
      acquiresleep(&b->lock); // 获取buffer的sleep lock
      return b; // 在目标桶中找到空闲的buffer
    }
  }
  //第三步：目标桶满了，需要从其他桶偷取
  release(&bcache[bucket].lock); // 释放桶的锁

  //遍历其他桶查找空闲的buffer
  for (int i = 0; i < NBUCKET; i++) {
    if (i == bucket) continue; // 跳过当前桶
    acquire(&bcache[i].lock); // 获取其他桶的锁
    for (b = bcache[i].head.prev; b != &bcache[i].head; b = b->prev) {
      if (b->refcnt == 0) {
        // 找到空闲的buffer
        //从当前的桶中移除
        b->next->prev = b->prev;
        b->prev->next = b->next;
        release(&bcache[i].lock); // 释放其他桶的锁

        acquire(&bcache[bucket].lock); // 重新获取目标桶的锁
        b->next = bcache[bucket].head.next;
        b->prev = &bcache[bucket].head;
        bcache[bucket].head.next->prev = b;
        bcache[bucket].head.next = b;
        //配置buffer
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        release(&bcache[bucket].lock); // 释放目标桶的锁
        acquiresleep(&b->lock); // 获取buffer的sleep lock
        return b;
      }
    }
    release(&bcache[i].lock); // 释放其他桶的锁
  }

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  // 先尝试根据blockno找到对应的桶
  int bucket = b->blockno % NBUCKET;
  acquire(&bcache[bucket].lock);
  
  // 检查buffer是否在预期的桶中
  struct buf *p;
  for (p = bcache[bucket].head.next; p != &bcache[bucket].head; p = p->next) {
    if (p == b) {
      // 找到了buffer
      b->refcnt--;
      if (b->refcnt == 0) {
        // no one is waiting for it.
        b->next->prev = b->prev;
        b->prev->next = b->next;
        b->next = bcache[bucket].head.next;
        b->prev = &bcache[bucket].head;
        bcache[bucket].head.next->prev = b;
        bcache[bucket].head.next = b;
      }
      release(&bcache[bucket].lock);
      return;
    }
  }
  release(&bcache[bucket].lock);

  // 如果在预期桶中没找到，说明buffer被移到了其他桶，需要遍历查找
  for (int i = 0; i < NBUCKET; i++) {
    if (i == bucket) continue; // 已经检查过了
    acquire(&bcache[i].lock);
    for (p = bcache[i].head.next; p != &bcache[i].head; p = p->next) {
      if (p == b) {
        b->refcnt--;
        if (b->refcnt == 0) {
          // no one is waiting for it.
          b->next->prev = b->prev;
          b->prev->next = b->next;
          b->next = bcache[i].head.next;
          b->prev = &bcache[i].head;
          bcache[i].head.next->prev = b;
          bcache[i].head.next = b;
        }
        release(&bcache[i].lock);
        return;
      }
    }
    release(&bcache[i].lock);
  }
  panic("brelse: buffer not found in any bucket");
}

void
bpin(struct buf *b) {
  // 先尝试根据blockno找到对应的桶
  int bucket = b->blockno % NBUCKET;
  acquire(&bcache[bucket].lock);
  
  // 检查buffer是否在预期的桶中
  struct buf *p;
  for (p = bcache[bucket].head.next; p != &bcache[bucket].head; p = p->next) {
    if (p == b) {
      b->refcnt++;
      release(&bcache[bucket].lock);
      return;
    }
  }
  release(&bcache[bucket].lock);

  // 如果在预期桶中没找到，遍历其他桶
  for (int i = 0; i < NBUCKET; i++) {
    if (i == bucket) continue; // 已经检查过了
    acquire(&bcache[i].lock);
    for (p = bcache[i].head.next; p != &bcache[i].head; p = p->next) {
      if (p == b) {
        b->refcnt++;
        release(&bcache[i].lock);
        return;
      }
    }
    release(&bcache[i].lock);
  }
  panic("bpin: buffer not found in any bucket");
}

void
bunpin(struct buf *b) {
  // 先尝试根据blockno找到对应的桶
  int bucket = b->blockno % NBUCKET;
  acquire(&bcache[bucket].lock);
  
  // 检查buffer是否在预期的桶中
  struct buf *p;
  for (p = bcache[bucket].head.next; p != &bcache[bucket].head; p = p->next) {
    if (p == b) {
      b->refcnt--;
      release(&bcache[bucket].lock);
      return;
    }
  }
  release(&bcache[bucket].lock);

  // 如果在预期桶中没找到，遍历其他桶
  for (int i = 0; i < NBUCKET; i++) {
    if (i == bucket) continue; // 已经检查过了
    acquire(&bcache[i].lock);
    for (p = bcache[i].head.next; p != &bcache[i].head; p = p->next) {
      if (p == b) {
        b->refcnt--;
        release(&bcache[i].lock);
        return;
      }
    }
    release(&bcache[i].lock);
  }
  panic("bunpin: buffer not found in any bucket");
}



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

// 哈希表中桶号索引，质数个桶可以减少哈希冲突（不会被大多数常数整除，不会固定落入某几个桶中）
#define NBUFMAP_BUCKET 13
// 求哈希索引，dev放在高位，blockno放在低位，减少冲突
#define BUFMAP_HASH(dev,blockno) (((dev)<<27|(blockno))%NBUFMAP_BUCKET)
struct {
  // struct spinlock lock;
  struct buf buf[NBUF];
  struct spinlock eviction_lock; // 驱逐锁

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.`
  // struct buf head;
  // 哈希表，13个桶指向冲突缓存区的链表首节点，每个桶一个锁
  struct buf bufmap[NBUFMAP_BUCKET];
  struct spinlock bufmap_locks[NBUFMAP_BUCKET];
} bcache;

void
binit(void)
{
  // struct buf *b;

  // initlock(&bcache.lock, "bcache");

  // // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }
  // 初始化桶锁
  for(int i=0;i<NBUFMAP_BUCKET;i++){
    initlock(&bcache.bufmap_locks[i],"bcache_bufmap");
    bcache.bufmap[i].next = 0; // 初始化桶的链表头为空
  }
  for(int i=0;i<NBUF;i++){
    // 初始化缓存区块
    struct buf* b = &bcache.buf[i];
    initsleeplock(&b->lock,"buffer");
    b->lastuse = 0;
    b->refcnt = 0;
    // 头插法添加到bufmap[0]
    b->next = bcache.bufmap[0].next;
    bcache.bufmap[0].next = b;
  }
  initlock(&bcache.eviction_lock,"bcache_eviction");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  // 获取索引号，获取锁
  uint key = BUFMAP_HASH(dev,blockno);
  acquire(&bcache.bufmap_locks[key]);

  // acquire(&bcache.lock);

  // Is the block already cached?
  // // 双向循环节点，循环一圈
  // for(b = bcache.head.next; b != &bcache.head; b = b->next){
  //   if(b->dev == dev && b->blockno == blockno){ // 该磁盘块已经被缓存在内存中
  //     b->refcnt++; // 增加引用计数，又有一个用户进程使用该缓冲区
  //     release(&bcache.lock); // 释放缓存链表的自旋锁
  //     acquiresleep(&b->lock); // 获取该缓冲区的睡眠锁，保证只有一个进程可以操作该缓冲区
  //     return b;
  //   }
  // }
  for(b = bcache.bufmap[key].next;b;b = b->next){
    if(b->dev == dev && b->blockno == blockno){ // 该磁盘块已经被缓存在内存中
      b->refcnt++; // 增加引用计数，又有一个用户进程使用该缓冲区
      release(&bcache.bufmap_locks[key]); // 释放桶锁
      acquiresleep(&b->lock); // 获取该缓冲区的睡眠锁，保证只有一个进程可以操作该缓冲区
      return b;
    }
  }
  // // Not cached.  未缓存至内存
  // // Recycle the least recently used (LRU) unused buffer. 该遍历顺序查找最近最久未被使用的buf作为缓冲区块
  // for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
  //   if(b->refcnt == 0) { // 找到一个未被使用的缓冲区
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1; // 增加引用计数，表示该缓冲区被使用
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }
  // 防止死锁（互相想获取对方桶里面的空闲buf）先释放当前桶锁
  release(&bcache.bufmap_locks[key]);
  // 防止blockno的缓存区块被重复创建，添加驱逐锁
  acquire(&bcache.eviction_lock); 
  // 添加驱逐锁期间可能创建了对应块，再检查一下是否存在
  for(b=bcache.bufmap[key].next;b;b=b->next){
    if(b->dev == dev && b->blockno == blockno){
      acquire(&bcache.bufmap_locks[key]);
      b->refcnt++;
      release(&bcache.bufmap_locks[key]);
      release(&bcache.eviction_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  struct buf* before_least = 0; // 记录LRU-BUF前一个节点
  uint holding_bucket = -1; // 记录当前持有哪个桶锁
  for(int i = 0;i < NBUFMAP_BUCKET;++i){
    acquire(&bcache.bufmap_locks[i]);
    int newfound = 0; // 标记是否找到新的最少使用buf
    for(b = &bcache.bufmap[i];b->next;b=b->next){
      if(b->next->refcnt == 0 && (!before_least || b->next->lastuse < before_least->next->lastuse)){
        before_least = b;
        newfound = 1;
      }
    }
    if(!newfound){ // 没有找到更少使用的buf，释放该桶锁
      release(&bcache.bufmap_locks[i]);
    }
    else{
      if(holding_bucket != -1) // 除了第一次找到的LRUbuf之外，释放之前持有的桶锁
        release(&bcache.bufmap_locks[holding_bucket]);
      holding_bucket = i;
    }
  }
  if(!before_least)
    panic("bget: no buffers");

  b=before_least->next;
  if(holding_bucket != key){ //如果占用的块不在key桶中
    before_least->next = b->next;
    release(&bcache.bufmap_locks[holding_bucket]);
    acquire(&bcache.bufmap_locks[key]);
    b->next = bcache.bufmap[key].next;
    bcache.bufmap[key].next = b;
  }
  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;
  b->refcnt = 1; // 增加引用计数
  release(&bcache.bufmap_locks[key]);
  release(&bcache.eviction_lock);
  acquiresleep(&b->lock);
  return b;
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

  uint key = BUFMAP_HASH(b->dev,b->blockno);
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  if(b->refcnt == 0){ // 没有人使用时记录时间戳ticks，表示上次使用截止时间
    b->lastuse = ticks;
  }

  // // acquire(&bcache.lock);
  // b->refcnt--;
  // if (b->refcnt == 0) {
  //   // no one is waiting for it.
  //   b->next->prev = b->prev;
  //   b->prev->next = b->next;
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }
  
  // release(&bcache.lock);
  release(&bcache.bufmap_locks[key]);
}

void
bpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev,b->blockno);
  acquire(&bcache.bufmap_locks[key]);
  // acquire(&bcache.lock);
  b->refcnt++;
  // release(&bcache.lock);
  release(&bcache.bufmap_locks[key]);
}

void
bunpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev,b->blockno);
  acquire(&bcache.bufmap_locks[key]);
  // acquire(&bcache.lock);
  b->refcnt--;
  // release(&bcache.lock);
  release(&bcache.bufmap_locks[key]);
}



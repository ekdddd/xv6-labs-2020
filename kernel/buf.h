struct buf { // 循环链表，最近最少使用缓存替换算法，最近最多使用靠近head
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  // struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
  uint lastuse; // 跟踪LRU-buf
};


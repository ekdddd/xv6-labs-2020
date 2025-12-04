// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  // 空闲列表保存从内核结束到PHYSTOP之间的每一页
  // void* 就是“通用指针”，可以指向任何数据类型
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// 把一个物理页（4KB）归还到物理页分配器（freelist）中，让以后可以被 kalloc() 再次分配。
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  // 覆盖整个页为 1 （垃圾值）的原因：可以帮助发现 “悬空引用（dangling pointer）”；如果某些代码继续访问已释放的页，会很快崩溃，有助于 debug
  // 悬空引用：指针指向一块已经被释放的内存
  memset(pa, 1, PGSIZE);

  // 把整个 4KB 物理页当成一个结构体，第一页 8 字节用来存 next，剩下的都是未使用空间。
  r = (struct run*)pa;

  acquire(&kmem.lock);
  // 头插法加入链表
  // kmem.freelist 保存空闲页链表的头指针，存放指向空闲物理页链表中“第一个节点（头节点）”的指针
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

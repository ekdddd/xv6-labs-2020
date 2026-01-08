// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define PA2PGREF_ID(p) (((p)-KERNBASE)/PGSIZE) // 由物理地址获取物理页id
#define PGREF_MAX_ENTRIES PA2PGREF_ID(PHYSTOP) // 物理页数上限

int pageref[PGREF_MAX_ENTRIES]; // 物理页引用计数数组，对应每个物理页
struct spinlock pgreflock; // 用于pageref数组的锁

#define PA2PGREF(p) pageref[PA2PGREF_ID((uint64)(p))] // 由物理地址获取物理页引用计数


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
  initlock(&pgreflock,"pgref"); // 初始化物理页引用计数锁
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
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&pgreflock);
  // Fill with junk to catch dangling refs.
  if(--PA2PGREF(pa) <= 0){ // 引用计数减1，若为0则真正释放物理页
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  } 
  release(&pgreflock);
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

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    PA2PGREF(r) = 1; // 新分配的物理页引用计数置为1  
  }
  return (void*)r;
}

// 物理页引用计数加1
void
krefpage(void *pa){
  acquire(&pgreflock);
  PA2PGREF(pa)++;
  release(&pgreflock);
}

// 处理写时复制页，返回处理后的物理页地址
void*
kcopy_n_deref(void *pa){
  acquire(&pgreflock);

  if(PA2PGREF(pa) <= 1){
    release(&pgreflock);
    return pa; // 引用计数为1，直接返回原物理页
  }
  // 引用计数大于1，分配新物理页并复制内容
  uint64 newpa = (uint64)kalloc();
  if(newpa == 0){
    release(&pgreflock);
    return 0; // 分配新物理页失败
  }
  memmove((void*)newpa,(void*)pa,PGSIZE); // 复制内容到新物理页
  
  // 引用计数加锁可以防止父进程在执行这部分之前（引用为2）子进程释放物理页面时（引用--）执行下一步引用计数减1，没有进程指向原物理页面导致内存泄漏
  PA2PGREF(pa)--; // 原物理页引用计数减1
  release(&pgreflock);
  return (void*)newpa;
}
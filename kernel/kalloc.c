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
// 给每个cpu都分配空闲页链表freelist和锁
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

char* kmem_lock_names[]={
  "kmem_cpu_0",
  "kmem_cpu_1",
  "kmem_cpu_2",
  "kmem_cpu_3",   
  "kmem_cpu_4",
  "kmem_cpu_5",
  "kmem_cpu_6",
  "kmem_cpu_7",
};

void
kinit()
{
  // initlock(&kmem.lock, "kmem");
  for(int i=0;i<NCPU;i++){
    initlock(&kmem[i].lock,kmem_lock_names[i]);
  }
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

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off(); // 关闭中断
  int cpu_id = cpuid();

  acquire(&kmem[cpu_id].lock);
  r->next = kmem[cpu_id].freelist;
  kmem[cpu_id].freelist = r;
  release(&kmem[cpu_id].lock);

  pop_off(); // 恢复中断
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off(); // 关闭中断
  int cpu_id = cpuid();

  acquire(&kmem[cpu_id].lock);
  // 可能出现互相尝试获取对方的空闲页导致死锁的情况（概率极低，尚未解决）
  // 可以添加一把锁用于偷页面，释放自己的锁避免死锁情况
  // acquire(&kmem[cpu].stealing_lock)
  // release(&kmem[cpu].lock)
  // ...steal pages...
  // acquire(&kmem[cpu].lock)
  // release(&kmem[cpu].stealing_lock)
  if(!kmem[cpu_id].freelist){ // 当前cpu的空闲链表为空，尝试从其他cpu窃取空闲页
    int steal_left = 64; // 指定偷64个内存页
    for(int i = 0;i < NCPU;++i){
      if(i == cpu_id) continue; // 跳过自己
      acquire(&kmem[i].lock);
      if(!kmem[i].freelist){ // 该cpu空闲链表为空，跳过
        release(&kmem[i].lock);
        continue;
      }
      struct run* rr = kmem[i].freelist;
      while(rr && steal_left){ // 从该cpu空闲链表中窃取内存页
        kmem[i].freelist = rr->next;
        rr->next = kmem[cpu_id].freelist;
        kmem[cpu_id].freelist = rr;
        rr = kmem[i].freelist;
        steal_left--;
      }
      release(&kmem[i].lock);
      if(steal_left == 0) break; // 偷到指定数量页面后退出循环
    }
  }
  r = kmem[cpu_id].freelist;
  if(r)
    kmem[cpu_id].freelist = r->next;
  release(&kmem[cpu_id].lock);
  pop_off(); // 恢复中断
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

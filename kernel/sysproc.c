#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_trace(void)
{
  // 用户调用 trace(mask) wrapper -> 把 mask 放到 a0，syscall number（系统调用号） 放 a7 -> 执行ecall进入内核 syscall() 分发到 sys_trace -> sys_trace 用 argint(0,&mask) 从 trapframe->a0 取出 mask 并写入 myproc()->kama_syscall_trace。
  int mask;
  if(argint(0, &mask) < 0)
    return -1;
  myproc()->kama_syscall_trace = mask; // 设置当前进程的系统调用追踪掩码
  return 0;
}

uint64
sys_sysinfo(void){
  struct sysinfo info;
  freebytes(&info.freemem); //获取空闲内存
  procnum(&info.nproc); // 获取进程数量
  uint64 dstaddr;
  // 从寄存器a0获取用户虚拟地址（用户态 sysinfo 程序自己内部定义的 struct sysinfo info 的虚拟地址会被放到 a0）
  argaddr(0,&dstaddr);
  // copyout(myproc()->pagetable, dstaddr, (char*)&info, sizeof(info)) 把内核的 info 拷贝到用户空间的 dstaddr 指向的位置
  if(copyout(myproc()->pagetable,dstaddr,(char*)&info,sizeof(info))<0){
    return -1;
  }
  return 0;
}
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    if (addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep_prepare(&ticks);
    release(&tickslock);
    sleep();
    acquire(&tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
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
sys_interpose(void)
{
  struct proc *p = myproc();
  if(p->mask!=0) return -1; //checks if this process already had some syscalls blocked
  int n;
  argint(0,&n); //to get the mask integer
  char path[MAXPATH];
  argstr(1,path,MAXPATH); //to get the string for path
  safestrcpy(p->allowedPath, path,MAXPATH); 
  p->mask = n;
  return 0; 
}

// pgaccess(base, len, mask): report which of the len pages starting at
// base have been accessed since the last call (PTE_A), as a bitmask
// copied to the user address mask. PTE_A is cleared after it is read.
uint64
sys_pgaccess(void)
{
  uint64 base, ubuf;
  int len;
  uint64 mask = 0;
  struct proc *p = myproc();

  argaddr(0, &base);
  argint(1, &len);
  argaddr(2, &ubuf);

  // bitmask is at most 64 bits wide.
  if (len < 0 || len > 64)
    return -1;

  for (int i = 0; i < len; i++) {
    uint64 va = PGROUNDDOWN(base) + (uint64)i * PGSIZE;
    if (va >= MAXVA)
      return -1;
    pte_t *pte = walk(p->pagetable, va, 0);
    if (pte == 0 || (*pte & PTE_V) == 0)
      continue;
    if (*pte & PTE_A) {
      mask |= 1ULL << i;
      *pte &= ~PTE_A; // clear so the next call sees only new accesses
    }
  }
  // the TLB may cache PTE_A already set; flush so future accesses set it again.
  sfence_vma();

  // copy only as many bytes as the caller's buffer holds.
  if (copyout(p->pagetable, p->sz, ubuf, (char *)&mask, (len + 7) / 8) < 0)
    return -1;
  return 0;
}

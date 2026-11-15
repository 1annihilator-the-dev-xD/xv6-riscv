#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[]; // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t)kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

#ifdef LAB_NET
  // PCI-E ECAM (configuration space), for pci.c
  kvmmap(kpgtbl, 0x30000000L, 0x30000000L, 0x10000000, PTE_R | PTE_W);

  // pci.c maps the e1000's registers here.
  kvmmap(kpgtbl, 0x40000000L, 0x40000000L, 0x20000, PTE_R | PTE_W);
#endif

#ifdef LAB_LOCK
  // CLINT
  kvmmap(kpgtbl, CLINT_BASE, CLINT_BASE, PGSIZE, PTE_R | PTE_W);
#endif

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext,
         PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);

  return kpgtbl;
}

// Initialize the kernel_pagetable, shared by all CPUs.
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch the current CPU's h/w page table register to
// the kernel's page table, and enable paging.
void
kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if (va >= MAXVA)
    panic("walk");

  for (int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    // a level-1 leaf is a 2MB superpage: return it, don't descend.
    if (level == 1 && (*pte & PTE_V) && (*pte & (PTE_R | PTE_W | PTE_X)))
      return pte;
    if (*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
#ifdef LAB_PGTBL
      if (PTE_LEAF(*pte)) {
        return pte;
      }
#endif
    } else {
      if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// If va lies in a 2MB superpage (valid level-1 leaf), return that
// level-1 PTE, otherwise 0. Never allocates.
static pte_t *
superpte(pagetable_t pagetable, uint64 va)
{
  if (va >= MAXVA)
    return 0;
  pte_t *pte = &pagetable[PX(2, va)];
  if ((*pte & PTE_V) == 0 || (*pte & (PTE_R | PTE_W | PTE_X)) != 0)
    return 0;
  pte = &((pagetable_t)PTE2PA(*pte))[PX(1, va)];
  if ((*pte & PTE_V) && (*pte & (PTE_R | PTE_W | PTE_X)))
    return pte;
  return 0;
}

// Map one 2MB superpage: va and pa must be 2MB-aligned.
// Returns 0 on success, -1 if a page-table page couldn't be allocated.
static int
mapsuper(pagetable_t pagetable, uint64 va, uint64 pa, int perm)
{
  if ((va % SUPERPGSIZE) != 0 || (pa % SUPERPGSIZE) != 0)
    panic("mapsuper: not aligned");

  pte_t *pte = &pagetable[PX(2, va)];
  pagetable_t l1;
  if (*pte & PTE_V) {
    l1 = (pagetable_t)PTE2PA(*pte);
  } else {
    if ((l1 = (pagetable_t)kalloc()) == 0)
      return -1;
    memset(l1, 0, PGSIZE);
    *pte = PA2PTE(l1) | PTE_V;
  }

  pte = &l1[PX(1, va)];
  if (*pte & PTE_V) {
    // a leftover, completely empty level-0 table (from an earlier
    // unmap) is fine: free it and replace it with the superpage.
    if (*pte & (PTE_R | PTE_W | PTE_X))
      panic("mapsuper: remap");
    pagetable_t l0 = (pagetable_t)PTE2PA(*pte);
    for (int i = 0; i < 512; i++)
      if (l0[i] & PTE_V)
        panic("mapsuper: remap");
    kfree(l0);
    *pte = 0;
  }
  *pte = PA2PTE(pa) | perm | PTE_V;
  return 0;
}

// Split a superpage into 512 ordinary 4KB mappings of the same memory.
// Needed when only part of a superpage is unmapped.
static void
demote(pte_t *l1)
{
  pagetable_t l0 = (pagetable_t)kalloc();
  if (l0 == 0)
    panic("demote: out of memory");
  uint64 pa = PTE2PA(*l1);
  int flags = PTE_FLAGS(*l1);
  for (int i = 0; i < 512; i++)
    l0[i] = PA2PTE(pa + (uint64)i * PGSIZE) | flags;
  *l1 = PA2PTE(l0) | PTE_V;
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    return 0;
  if ((*pte & PTE_V) == 0)
    return 0;
  if ((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  // inside a superpage the PTE gives the 2MB base: add the offset.
  if (superpte(pagetable, va) != 0)
    pa += va & (SUPERPGSIZE - 1);
  return pa;
}


// Recursive helper for vmprint: print every valid PTE of one table.
// level 0 is the root; indentation shows depth.
static void
vmprint_level(pagetable_t pagetable, int level)
{
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) == 0)
      continue;
    for (int j = 0; j <= level; j++)
      printk(" ..");
    printk("%d: pte %p pa %p\n", i, (void *)pte, (void *)PTE2PA(pte));
    // a PTE with no R/W/X bits points to a lower-level page table.
    if ((pte & (PTE_R | PTE_W | PTE_X)) == 0)
      vmprint_level((pagetable_t)PTE2PA(pte), level + 1);
  }
}

// Print a page table in the format the pgtbl lab expects.
void
vmprint(pagetable_t pagetable)
{
  printk("page table %p\n", pagetable);
  vmprint_level(pagetable, 0);
}



// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if (mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.

int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if ((va % PGSIZE) != 0)
    panic("mappages: va not aligned");

  if ((size % PGSIZE) != 0)
    panic("mappages: size not aligned");

  if (size == 0)
    panic("mappages: size");

  a = va;
  last = va + size - PGSIZE;
  for (;;) {
    if ((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if (*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}
// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t)kalloc();
  if (pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. It's OK if the mappings don't exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if ((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  uint64 end = va + npages * PGSIZE;
  for (a = va; a < end;) {
    pte_t *l1 = superpte(pagetable, a);
    if (l1 != 0) {
      if ((a % SUPERPGSIZE) == 0 && a + SUPERPGSIZE <= end) {
        // the whole superpage is being unmapped.
        if (do_free)
          superfree((void *)PTE2PA(*l1));
        *l1 = 0;
        a += SUPERPGSIZE;
        continue;
      }
      // only part of it: split it, then take the 4KB path below.
      demote(l1);
    }
    if ((pte = walk(pagetable, a, 0)) == 0) { // leaf page table entry allocated?
      a += PGSIZE;
      continue;
    }
    if ((*pte & PTE_V) == 0) { // has physical page been allocated?
      a += PGSIZE;
      continue;
    }
    if (do_free) {
      uint64 pa = PTE2PA(*pte);
      kfree((void *)pa);
    }
    *pte = 0;
    a += PGSIZE;
  }
}


// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if (newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for (a = oldsz; a < newsz;) {
    // use a 2MB superpage when a is 2MB-aligned and a full one fits.
    if ((a % SUPERPGSIZE) == 0 && newsz - a >= SUPERPGSIZE) {
      char *sm = superalloc(); // already zeroed
      if (sm != 0) {
        if (mapsuper(pagetable, a, (uint64)sm, PTE_R | PTE_U | xperm) != 0) {
          superfree(sm);
          uvmdealloc(pagetable, a, oldsz);
          return 0;
        }
        a += SUPERPGSIZE;
        continue;
      }
      // no superpages left: fall back to ordinary pages.
    }
    mem = kalloc();
    if (mem == 0) {
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R | PTE_U | xperm) !=
        0) {
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    a += PGSIZE;
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if (newsz >= oldsz)
    return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if (pte & PTE_V) {
      // backtrace();
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if (sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for (i = 0; i < sz;) {
    pte_t *l1 = superpte(old, i);
    if (l1 != 0) {
      // copy a whole superpage into a fresh superpage.
      char *sm = superalloc();
      if (sm == 0)
        goto err;
      memmove(sm, (char *)PTE2PA(*l1), SUPERPGSIZE);
      if (mapsuper(new, i, (uint64)sm, PTE_FLAGS(*l1) & ~PTE_V) != 0) {
        superfree(sm);
        goto err;
      }
      i += SUPERPGSIZE;
      continue;
    }
    if ((pte = walk(old, i, 0)) == 0) {
      i += PGSIZE;
      continue; // page table entry hasn't been allocated
    }
    if ((*pte & PTE_V) == 0) {
      i += PGSIZE;
      continue; // physical page hasn't been allocated
    }
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if ((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char *)pa, PGSIZE);
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0) {
      kfree(mem);
      goto err;
    }
    i += PGSIZE;
  }
  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 psz, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while (len > 0) {
    va0 = PGROUNDDOWN(dstva);
    if (va0 >= MAXVA)
      return -1;

    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) {
      if ((pa0 = vmfault(pagetable, psz, va0, 0)) == 0) {
        return -1;
      }
    }

    if ((pte = walk(pagetable, va0, 0)) == 0) {
      // printk("copyout: pte should exist %lx %ld\n", dstva, len);
      return -1;
    }


    // forbid copyout over read-only user text pages.
    if ((*pte & PTE_W) == 0)
      return -1;

    n = PGSIZE - (dstva - va0);
    if (n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, uint64 psz, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while (len > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) {
      if ((pa0 = vmfault(pagetable, psz, va0, 1)) == 0) {
        return -1;
      }
    }
    n = PGSIZE - (srcva - va0);
    if (n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, uint64 psz, char *dst, uint64 srcva,
          uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while (got_null == 0 && max > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) {
      if ((pa0 = vmfault(pagetable, psz, va0, 1)) == 0) {
        return -1;
      }
    }
    n = PGSIZE - (srcva - va0);
    if (n > max)
      n = max;

    char *p = (char *)(pa0 + (srcva - va0));
    while (n > 0) {
      if (*p == '\0') {
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if (got_null) {
    return 0;
  } else {
    return -1;
  }
}


// allocate and map user memory if process is referencing a page
// that was lazily allocated in sys_sbrk().
// returns 0 if va is invalid or already mapped, or if
// out of physical memory, and physical address if successful.
uint64
vmfault(pagetable_t pagetable, uint64 psz, uint64 va, int read)
{
  uint64 mem;


  if (va >= psz)
    return 0;
  va = PGROUNDDOWN(va);
  if (ismapped(pagetable, va)) {
    return 0;
  }
  mem = (uint64)kalloc();
  if (mem == 0)
    return 0;
  memset((void *)mem, 0, PGSIZE);
  if (mappages(pagetable, va, PGSIZE, mem, PTE_W | PTE_U | PTE_R) != 0) {
    kfree((void *)mem);
    return 0;
  }
  return mem;
}

int
ismapped(pagetable_t pagetable, uint64 va)
{
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0) {
    return 0;
  }
  if (*pte & PTE_V) {
    return 1;
  }
  return 0;
}



#ifdef LAB_PGTBL
pte_t *
pgpte(pagetable_t pagetable, uint64 va)
{
  return walk(pagetable, va, 0);
}


void
cnt_level(pagetable_t pagetable, uint64 *nsuper, uint64 *n2, uint64 va_start, int level)
{
  uint64 va;
  int n = (level == 3) ? 1 : 512;

  for (int i = 0; i < n; i++) {
    pte_t pte = pagetable[i];
    if (pte & PTE_V) {
      va = (((uint64)i) << PXSHIFT(level)) + va_start;
      uint64 child = PTE2PA(pte);
      if (level > 0) {
        if (!PTE_LEAF(pte)) {
          cnt_level((pagetable_t)child, nsuper, n2, va, level-1);
        } else {
          *nsuper += 1;
        }
      } else {
        *n2 += 1;
      }
    }
  }
}

uint64
sys_ksupernpte() {
  struct proc *p;
  uint64 as, an;
  uint64 s = 0, n = 0;

  cnt_level(kernel_pagetable, &s, &n, 0, 2);

  p = myproc();
  argaddr(0, &as);
  argaddr(1, &an);

  if (copyout(p->pagetable, p->sz, as, (char *)&s, sizeof(uint64)) < 0) {
    return -1;
  }
  if (copyout(p->pagetable, p->sz, an, (char *)&n, sizeof(uint64)) < 0) {
    return -1;
  }

  return 0;
}
#endif

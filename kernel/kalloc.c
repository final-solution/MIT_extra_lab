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

struct {
  struct spinlock lock;
  uint64 ref[INDEX_MAX];
} refs;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&refs.lock, "refs");

  for (int i = 0; i < INDEX_MAX; i++)
  {
    refs.ref[i] = 1;
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

  acquire(&refs.lock);
  refs.ref[INDEX((uint64)pa)]--;
  if (get_refs((uint64)pa) > 0)
  {
    release(&refs.lock);
    return;
  }
  release(&refs.lock);
  
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
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
  {
    kmem.freelist = r->next;
    refs.ref[INDEX((uint64)r)] = 1;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// 更新页表引用次数数组refs，更新方式由mode决定
// =1,refs++
// =0,refs=1
// =-1,refs--
void
change_refs(uint64 pa, int mode)
{
  acquire(&refs.lock);
  switch (mode)
  {
    case 1:
      refs.ref[INDEX(pa)]++;
      break;
    case 0:
      refs.ref[INDEX(pa)] = 1;
      break;
    case -1:
      refs.ref[INDEX(pa)]--;
      break;  
    default:
      break;
  }
  release(&refs.lock);
}

// 返回页表引用次数数组相应索引的值
uint64
get_refs(uint64 pa)
{
  return refs.ref[INDEX(pa)];
}
#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
#include <bitmap.h>
#include "threads/synch.h"
struct page;
enum vm_type;

struct anon_page {
    enum vm_type type;
    void *va;
    struct frame *frame;
    struct page_info *aux;
};

struct page_swap{
  struct bitmap *swap_map;
  struct lock swap_lock;
};



void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif

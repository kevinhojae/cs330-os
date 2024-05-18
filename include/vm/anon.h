#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
#include "lib/kernel/bitmap.h"
#include "threads/synch.h"
struct page;
enum vm_type;


struct anon_page {
    enum vm_type type;
    void *va;
    int swap_table_index;
};

//swap slot들이 어떻게 쓰이는지 확인하기 위함.
//bitmap을 통해 사용되는 공간과 사용되지 않는 공간을 구분
struct page_swap{
  struct bitmap *swap_map;
  struct lock swap_lock;
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);
void anon_destroy (struct page *page);

#endif

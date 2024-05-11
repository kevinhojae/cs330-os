#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
struct page;
enum vm_type;

struct anon_page {
  struct frame *frame;
  struct page *page;
  bool writable;
  bool swapped;
  size_t swap_slot;
  void *kva;
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);
void anon_destroy (struct page *page);

#endif

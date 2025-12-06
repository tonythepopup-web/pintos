#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include <hash.h>

#define VM_BIN 0
#define VM_FILE 1
#define VM_ANON 2

struct page {
    int type;
    void *vaddr;
    bool write_enable;
    struct file* file;
    struct list_elem mmap_elem;
    bool is_loaded;
    size_t offset;
    size_t read_bytes;
    size_t zero_bytes;
    size_t swap_table;
    struct hash_elem helem;
    bool pinned;
};

struct mmap_file {
    struct file* file;
    unsigned map_id;
    struct list_elem elem;
    struct list spte_list;
};

void page_init (struct hash *page);
bool insert_page (struct hash *page, struct page *page_entry);
bool delete_page (struct hash *page, struct page *page_entry);
struct page *find_spte (void *vaddr);
void page_destroy (struct hash *page);
void page_destroy_func (struct hash_elem *e, void *aux);

#endif

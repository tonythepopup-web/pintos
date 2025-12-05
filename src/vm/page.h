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

// mapping된 file의 정보를 저장하는 구조체
struct mmap_file {
    struct file* file;      // mmap_file의 file 객체
    unsigned map_id;        // mmap_file의 id
    struct list_elem elem;  // mmap_file list element
    struct list spte_list;  // mmap_file에 해당하는 모든 spte
};

void page_init (struct hash *page);
static unsigned page_hash_func (const struct hash_elem *e, void *aux);
static bool page_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux);
bool insert_page (struct hash *page, struct page *page_entry);
bool delete_page (struct hash *page, struct page *page_entry);
struct page *find_spte (void *vaddr);
void page_destroy (struct hash *page);
void page_destroy_func (struct hash_elem *e, void *aux);
void check_valid_buffer (void *buffer, unsigned size, void *esp, bool to_write);

#endif
#include "vm/page.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include <string.h>
#include "vm/frame.h"

extern struct lock file_lock;


void page_init (struct hash *page) {
    hash_init(page, page_hash_func, page_less_func, NULL);
}

static unsigned page_hash_func (const struct hash_elem *e, void *aux) {
    return hash_int(hash_entry(e, struct page, helem)->vaddr);
}

static bool page_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux) {
    if (hash_entry(a, struct page, helem)->vaddr < hash_entry(b, struct page, helem)->vaddr)
        return true;
    else
        return false;
}

bool insert_page (struct hash *page, struct page *page_entry) {
    if (hash_insert(page, &page_entry->helem) == NULL)
        return true;
    else
        return false;
}

// swap table 구현 이후 수정 필요
// page(spt entry) 할당 해제 구현 필요
bool delete_page (struct hash *page, struct page *page_entry) {
    if (!hash_delete(page, &page_entry->helem))
        return false;
    free_frame(pagedir_get_page(thread_current()->pagedir, page_entry->vaddr));
    free(page_entry);
    return true;
}

struct page *find_spte (void *vaddr) {
    struct hash *page = &thread_current()->spt; // 지금 실행 중인 스레드의 spt 해시테이블
    struct page spte;
    spte.vaddr = pg_round_down(vaddr); //vaddr 이 어느 페이지에 속하는지 확인한다
    struct hash_elem *elem = hash_find(page, &spte.helem);
    if (elem) {
        return hash_entry(elem, struct page, helem);
    }
    else {
        return NULL;
    }
}

void page_destroy (struct hash *page) {
    hash_destroy(page, page_destroy_func);
}

// swap table 구현 이후 수정 필요
// page(spt entry) 할당 해제 구현 필요
void page_destroy_func (struct hash_elem *e, void *aux) {
    struct page *spte = hash_entry(e, struct page, helem);
    if (spte == NULL)
        return;
    if (spte->is_loaded)
        free_frame(pagedir_get_page(thread_current()->pagedir, spte->vaddr));
    free(spte);
}

bool load_file (void *kaddr, struct page *spte) {
    if (lock_held_by_current_thread(&file_lock)) {
        if (file_read_at(spte->file, kaddr, spte->read_bytes, spte->offset) != spte->read_bytes)
            return false;
        return true;
    }
    else {
        lock_acquire(&file_lock);
        if (file_read_at(spte->file, kaddr, spte->read_bytes, spte->offset) != spte->read_bytes) {
            lock_release(&file_lock);
            return false;
        }
        lock_release(&file_lock);
        return true;
    }
}
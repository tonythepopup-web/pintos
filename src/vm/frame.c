#include "vm/frame.h"
#include "threads/synch.h"
#include "lib/kernel/bitmap.h"
#include "devices/block.h"
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "filesys/file.h"

extern struct lock file_lock;

struct list frame_list;
struct lock frame_lock;
struct list_elem *clock_ptr;

struct lock swap_lock;
struct bitmap *swap_table;
struct block *swap_disk;
size_t swap_slot_count;

void frame_init (void) {
    list_init(&frame_list);
    lock_init(&frame_lock);
    clock_ptr = NULL;
}

void insert_frame (struct frame *frame) {
    lock_acquire(&frame_lock);
    list_push_back(&frame_list, &frame->elem);
    lock_release(&frame_lock);
}

void delete_frame (struct frame *frame) {
    ASSERT (lock_held_by_current_thread(&frame_lock));
    if (clock_ptr == &frame->elem) {
        clock_ptr = get_next_clock_ptr();
        list_remove(&frame->elem);
    }
    else
        list_remove(&frame->elem);
}

static struct list_elem *get_next_clock_ptr (void) {
    ASSERT (lock_held_by_current_thread(&frame_lock));

    if (clock_ptr == NULL || clock_ptr == list_end(&frame_list)) {
        if (!list_empty(&frame_list)) {
            clock_ptr = list_begin(&frame_list);
            return clock_ptr;
        }
        else 
            return NULL;
    }

    clock_ptr = list_next(clock_ptr);
    if (clock_ptr == list_end(&frame_list))
        return get_next_clock_ptr();

    return clock_ptr;
}


struct frame *alloc_frame (enum palloc_flags flag) {
    struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
    if (frame == NULL)
        return NULL;
    memset(frame, 0, sizeof(struct frame));
    frame->kaddr = palloc_get_page(flag);
    frame->t = thread_current();
    
    // 공간이 부족해 frame을 victim해야 할 경우
    while (frame->kaddr == NULL) {
        lock_acquire(&frame_lock);
        struct list_elem *e = get_next_clock_ptr();
        struct frame *victim = list_entry(e, struct frame, elem);

        // victim frame 선택 (clock algorithm)
        while (victim->spte->pinned || pagedir_is_accessed(victim->t->pagedir, victim->spte->vaddr)) {
            pagedir_set_accessed(victim->t->pagedir, victim->spte->vaddr, false);
            e = get_next_clock_ptr();

            victim = list_entry(e, struct frame, elem);
        }
        
        // type별 swap out 처리
        switch (victim->spte->type) {
            case VM_BIN:
                if (pagedir_is_dirty(victim->t->pagedir, victim->spte->vaddr)) {
                    victim->spte->type = VM_ANON;
                    victim->spte->swap_table = swap_out(victim->kaddr);
                }
                break;
                
            case VM_FILE:
                if (pagedir_is_dirty(victim->t->pagedir, victim->spte->vaddr)) {
                    lock_acquire(&file_lock);
                    file_write_at(victim->spte->file, victim->spte->vaddr, victim->spte->read_bytes, victim->spte->offset);
                    lock_release(&file_lock);
                }
                    
                break;
            case VM_ANON:
                victim->spte->swap_table = swap_out(victim->kaddr);
                break; 
        }
        victim->spte->is_loaded = false;
        __free_frame(victim);
        lock_release(&frame_lock);

        frame->kaddr = palloc_get_page(flag);
    }
    insert_frame(frame);
    return frame;
}

void __free_frame (struct frame *frame) {
    ASSERT (lock_held_by_current_thread(&frame_lock));    

    pagedir_clear_page(frame->t->pagedir, frame->spte->vaddr);
    delete_frame(frame);
    palloc_free_page(frame->kaddr);
    free(frame);
}

void free_frame (void *kaddr) {
    lock_acquire(&frame_lock);

    // 제거할 frame 탐색
    struct frame *frame = NULL;
    for (struct list_elem *e = list_begin(&frame_list); e != list_end(&frame_list); e = list_next(e)) {
        if (list_entry(e, struct frame, elem)->kaddr == kaddr) {
            frame = list_entry(e, struct frame, elem);
            break;
        }
    }
    if (frame != NULL)
        __free_frame(frame);
    
    lock_release(&frame_lock);
}

void swap_init(void){
    swap_disk = block_get_role(BLOCK_SWAP);
    lock_init(&swap_lock);
    swap_slot_count = block_size(swap_disk) / 8; //sector size = 512 B, slot size = 4 KB. 따라서 slot 하나당 sector 8개
    swap_table = bitmap_create(swap_slot_count);
}

void swap_in(size_t used_index, void* kaddr){
    lock_acquire(&swap_lock);
    lock_acquire(&file_lock);

    size_t index_sector = used_index * 8;
    void* buf = kaddr;
    
    for(int i = 0; i < 8; i++){
        block_read(swap_disk, index_sector, buf);
        index_sector++;
        buf += BLOCK_SECTOR_SIZE;
    }
    bitmap_set(swap_table, used_index, 0);

    lock_release(&file_lock);
    lock_release(&swap_lock);
}

size_t swap_out(void* kaddr){
    lock_acquire(&swap_lock);
    lock_acquire(&file_lock);

    size_t index_empty = bitmap_scan_and_flip(swap_table, 0, 1, 0);
    if (index_empty == BITMAP_ERROR || index_empty >= swap_slot_count) {
        lock_release(&file_lock);
        lock_release(&swap_lock);
        return BITMAP_ERROR;
    }

    size_t index_sector = index_empty * 8;
    void* buf = kaddr;
    
    for(int i = 0; i < 8; i++){
        block_write(swap_disk, index_sector, buf);
        index_sector++;
        buf += BLOCK_SECTOR_SIZE;
    }

    lock_release(&file_lock);
    lock_release(&swap_lock);

    return index_empty;
}
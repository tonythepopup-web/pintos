#include "vm/frame.h"
#include "threads/synch.h"
#include "lib/kernel/bitmap.h"
#include "devices/block.h"
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include <string.h>

extern struct lock file_lock;

static struct list frame_table;
static struct lock frame_table_lock;
static struct list_elem *clock_hand;

static struct bitmap *swap_bitmap;
static struct block *swap_block;
static struct lock swap_lock;
static size_t swap_size;

static struct list_elem *clock_get_next (void);
static void __free_frame (struct frame *f);

void 
frame_init (void) 
{
    list_init(&frame_table);
    lock_init(&frame_table_lock);
    clock_hand = NULL;
}

void 
swap_init(void)
{
    swap_block = block_get_role(BLOCK_SWAP);
    swap_size = block_size(swap_block) / 8;
    swap_bitmap = bitmap_create(swap_size);
    lock_init(&swap_lock);
}

void 
insert_frame (struct frame *f) 
{
    lock_acquire(&frame_table_lock);
    list_push_back(&frame_table, &f->elem);
    lock_release(&frame_table_lock);
}

void 
delete_frame (struct frame *f) 
{
    ASSERT (lock_held_by_current_thread(&frame_table_lock));
    
    if (clock_hand == &f->elem) {
        clock_hand = clock_get_next();
    }
    list_remove(&f->elem);
}

static struct list_elem *
clock_get_next (void) 
{
    ASSERT (lock_held_by_current_thread(&frame_table_lock));

    if (list_empty(&frame_table)) 
        return NULL;

    if (clock_hand == NULL || clock_hand == list_end(&frame_table)) {
        clock_hand = list_begin(&frame_table);
        return clock_hand;
    }

    clock_hand = list_next(clock_hand);
    if (clock_hand == list_end(&frame_table))
        return clock_get_next();

    return clock_hand;
}

struct frame *
alloc_frame (enum palloc_flags flag) 
{
    struct frame *new_frame = (struct frame *)malloc(sizeof(struct frame));
    if (new_frame == NULL)
        return NULL;
    
    memset(new_frame, 0, sizeof(struct frame));
    new_frame->t = thread_current();
    new_frame->kaddr = palloc_get_page(flag);
    
    while (new_frame->kaddr == NULL) {
        struct list_elem *e;
        struct frame *victim;
        
        lock_acquire(&frame_table_lock);
        e = clock_get_next();
        victim = list_entry(e, struct frame, elem);

        while (victim->spte->pinned || 
               pagedir_is_accessed(victim->t->pagedir, victim->spte->vaddr)) {
            pagedir_set_accessed(victim->t->pagedir, victim->spte->vaddr, false);
            e = clock_get_next();
            victim = list_entry(e, struct frame, elem);
        }
        
        if (victim->spte->type == VM_BIN) {
            if (pagedir_is_dirty(victim->t->pagedir, victim->spte->vaddr)) {
                victim->spte->type = VM_ANON;
                victim->spte->swap_table = swap_out(victim->kaddr);
            }
        }
        else if (victim->spte->type == VM_FILE) {
            if (pagedir_is_dirty(victim->t->pagedir, victim->spte->vaddr)) {
                lock_acquire(&file_lock);
                file_write_at(victim->spte->file, victim->spte->vaddr, 
                              victim->spte->read_bytes, victim->spte->offset);
                lock_release(&file_lock);
            }
        }
        else if (victim->spte->type == VM_ANON) {
            victim->spte->swap_table = swap_out(victim->kaddr);
        }
        
        victim->spte->is_loaded = false;
        __free_frame(victim);
        lock_release(&frame_table_lock);

        new_frame->kaddr = palloc_get_page(flag);
    }
    
    insert_frame(new_frame);
    return new_frame;
}

void 
free_frame (void *kaddr) 
{
    struct frame *target_frame = NULL;
    struct list_elem *e;
    
    lock_acquire(&frame_table_lock);

    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)) {
        struct frame *f = list_entry(e, struct frame, elem);
        if (f->kaddr == kaddr) {
            target_frame = f;
            break;
        }
    }
    
    if (target_frame != NULL)
        __free_frame(target_frame);
    
    lock_release(&frame_table_lock);
}

static void 
__free_frame (struct frame *f) 
{
    ASSERT (lock_held_by_current_thread(&frame_table_lock));    

    pagedir_clear_page(f->t->pagedir, f->spte->vaddr);
    delete_frame(f);
    palloc_free_page(f->kaddr);
    free(f);
}

void 
swap_in(size_t used_index, void* kaddr)
{
    size_t sector_idx = used_index * 8;
    void* buffer = kaddr;
    int i;
    
    lock_acquire(&swap_lock);
    lock_acquire(&file_lock);
    
    for(i = 0; i < 8; i++){
        block_read(swap_block, sector_idx, buffer);
        sector_idx++;
        buffer += BLOCK_SECTOR_SIZE;
    }
    
    bitmap_set(swap_bitmap, used_index, 0);

    lock_release(&file_lock);
    lock_release(&swap_lock);
}

size_t 
swap_out(void* kaddr)
{
    size_t free_slot;
    size_t sector_idx;
    void* buffer = kaddr;
    int i;
    
    lock_acquire(&swap_lock);
    lock_acquire(&file_lock);

    free_slot = bitmap_scan_and_flip(swap_bitmap, 0, 1, 0);
    if (free_slot == BITMAP_ERROR || free_slot >= swap_size) {
        lock_release(&file_lock);
        lock_release(&swap_lock);
        return BITMAP_ERROR;
    }

    sector_idx = free_slot * 8;
    
    for(i = 0; i < 8; i++){
        block_write(swap_block, sector_idx, buffer);
        sector_idx++;
        buffer += BLOCK_SECTOR_SIZE;
    }

    lock_release(&file_lock);
    lock_release(&swap_lock);

    return free_slot;
}

#include "userprog/syscall.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "vm/frame.h"

static void syscall_handler (struct intr_frame *);
void pin(char *start, char *end);
void unpin(char *start, char *end);
bool handle_page_fault (struct page *spte);
struct page *find_spte (void *vaddr);

struct lock file_lock;

struct file_info {
  struct file *file;
  int fd;
  struct list_elem elem;
};

struct file_info* search(struct list* file_list, int fd) {
  for (struct list_elem *e = list_begin (file_list); e != list_end (file_list); e = list_next (e)) {
    struct file_info *f = list_entry(e, struct file_info, elem);
    if (f->fd == fd)
      return f;
  }
  return NULL;
}

void close_files(struct list *file_list) {
  struct list_elem *e;
	while(!list_empty(file_list)) {
		e = list_pop_front(file_list);
		struct file_info *f = list_entry(e, struct file_info, elem);
	  file_close(f->file);
	  list_remove(e);
	  free(f);
	}
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  uint32_t *esp = f->esp;
  int args0, args1, args2;

  if (esp == NULL || is_kernel_vaddr(esp)) {
    exit(-1);
  }

  switch (*esp) {
    case SYS_HALT:
      halt();
      break;

    case SYS_EXIT:
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1)) {
        exit(-1);
      }
      args0 = *(esp + 1);
      exit(args0);
      break;

    case SYS_EXEC:
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1)) {
        exit(-1);
      }
      args0 = *(esp + 1);
      f->eax = exec((const char *)args0);
      break;

    case SYS_WAIT:
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1)) {
        exit(-1);
      }
      args0 = *(esp + 1);
      f->eax = wait((pid_t)args0);
      break;

    case SYS_CREATE:
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1)) {
        exit(-1);
      }
      if (esp + 2 == NULL || is_kernel_vaddr(esp + 2)) {
        exit(-1);
      }
      args0 = *(esp + 1);
      args1 = *(esp + 2);
      f->eax = create((const char *)args0, (unsigned)args1);
      break;

    case SYS_REMOVE:
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1)) {
        exit(-1);
      }
      args0 = *(esp + 1);
      f->eax = remove((const char *)args0);
      break;

    case SYS_OPEN:
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1)) {
        exit(-1);
      }
      args0 = *(esp + 1);
      f->eax = open((const char *)args0);
      break;

    case SYS_FILESIZE:
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1)) {
        exit(-1);
      }
      args0 = *(esp + 1);
      f->eax = filesize((int)args0);
      break;

    case SYS_READ:
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1)) {
        exit(-1);
      }
      if (esp + 2 == NULL || is_kernel_vaddr(esp + 2)) {
        exit(-1);
      }
      if (esp + 3 == NULL || is_kernel_vaddr(esp + 3)) {
        exit(-1);
      }
      args0 = *(esp + 1);
      args1 = *(esp + 2);
      args2 = *(esp + 3);
      f->eax = read((int)args0, (void *)args1, (unsigned)args2);
      break;

    case SYS_WRITE:
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1)) {
        exit(-1);
      }
      if (esp + 2 == NULL || is_kernel_vaddr(esp + 2)) {
        exit(-1);
      }
      if (esp + 3 == NULL || is_kernel_vaddr(esp + 3)) {
        exit(-1);
      }
      args0 = *(esp + 1);
      args1 = *(esp + 2);
      args2 = *(esp + 3);
      f->eax = write((int)args0, (const void *)args1, (unsigned)args2);
      break;

    case SYS_SEEK:
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1)) {
        exit(-1);
      }
      if (esp + 2 == NULL || is_kernel_vaddr(esp + 2)) {
        exit(-1);
      }
      args0 = *(esp + 1);
      args1 = *(esp + 2);
      seek((int)args0, (unsigned)args1);
      break;

    case SYS_TELL:
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1)) {
        exit(-1);
      }
      args0 = *(esp + 1);
      f->eax = tell((int)args0);
      break;

    case SYS_CLOSE:
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1)) {
        exit(-1);
      }
      args0 = *(esp + 1);
      close((int)args0);
      break;

    case SYS_MMAP:
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1)) {
        exit(-1);
      }
      if (esp + 2 == NULL || is_kernel_vaddr(esp + 2)) {
        exit(-1);
      }
      args0 = *(esp + 1);
      args1 = *(esp + 2);
      f->eax = mmap((int)args0, (void *)args1);
      break;

    case SYS_MUNMAP:
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1)) {
        exit(-1);
      }
      args0 = *(esp + 1);
      munmap((mapid_t)args0);
      break;
  }
}

void halt() {
  shutdown_power_off();
}

void exit(int status){
  struct thread *t = thread_current();

  printf("%s: exit(%d)\n", t->name, status);

  thread_current()->exit_status = status;
  
  thread_exit();
}

pid_t exec(const char *cmdline) {
  return process_execute(cmdline);
}

int wait(pid_t pid) {
  return process_wait(pid);
}

bool create(const char *file, unsigned initial_size) {
  if (file == NULL)
    exit(-1);
  return filesys_create(file, initial_size);
}

bool remove(const char *file) {
  if (file == NULL)
    exit(-1);
  return filesys_remove(file);
}

int open(const char *file) {
  if (file == NULL || is_kernel_vaddr(file)) {
    exit(-1);
  }
  lock_acquire(&file_lock);
  struct file *open_file = filesys_open(file);
  if (open_file == NULL) {
    lock_release(&file_lock);
    return -1;
  }

  int fd_idx = thread_current()->fd_count;
  if (fd_idx >= 128) {
    file_close(open_file);
    lock_release(&file_lock);
    return -1;
  }
  thread_current()->fd_count += 1;
    
  struct file_info *tmp_file = malloc(sizeof(struct file_info *));
  tmp_file->file = open_file;
  tmp_file->fd = fd_idx;
  list_push_back(&thread_current()->file_list, &tmp_file->elem);
  lock_release(&file_lock);
  return fd_idx;
}

int filesize(int fd)
{
  struct file_info *f_info = search(&thread_current()->file_list, fd);
  struct file *f = f_info->file;
  if (f == NULL)
    exit(-1);
  return file_length(f);
}

void pin(char *start, char *end)
{
  char *i;
  for (i = start; i < end; i += PGSIZE) {
    struct page *spte = find_spte(i);
    spte->pinned = true;
    if (spte->is_loaded == false)
      handle_page_fault(spte);
  }
}

void unpin(char *start, char *end)
{
  char *i;
  for (i = start; i < end; i += PGSIZE)
    find_spte(i)->pinned = false;
}

int read(int fd, void *buffer, unsigned size) {
  if (buffer == NULL || is_kernel_vaddr(buffer)) {
    exit(-1);
  }
  pin(buffer, buffer + size);
  lock_acquire(&file_lock);
  if (fd == 0) {
    unsigned idx = 0;
    char w;
    for (; idx < size; idx++) {
      w = input_getc();
      ((char *)buffer)[idx] = w;
      if (w == '\0')
        break;
    }
    unpin(buffer, buffer + size);
    lock_release(&file_lock);
    return idx;
  }
  if (fd == 1) {
    unpin(buffer, buffer + size);
    lock_release(&file_lock);
    return -1;
  }

  struct file_info *f_info = search(&thread_current()->file_list, fd);
  struct file *f = f_info->file;
  if (f == NULL) {
    unpin(buffer, buffer + size);
    lock_release(&file_lock);
    return -1;
  }
  int read_size_byte = file_read(f, buffer, size);
  unpin(buffer, buffer + size);
  lock_release(&file_lock);
  return read_size_byte;
}

int write(int fd, const void *buffer, unsigned size) {
  if (buffer == NULL || is_kernel_vaddr(buffer)) {
    exit(-1);
  }
  pin(buffer, buffer + size);
  lock_acquire(&file_lock);
  if (fd == 1) {
    putbuf(buffer, size);
    lock_release(&file_lock);
    unpin(buffer, buffer + size);
    return size;
  }
  if (fd == 0) {
    lock_release(&file_lock);
    unpin(buffer, buffer + size);
    return 0;
  }

  struct file_info *f_info = search(&thread_current()->file_list, fd);
  struct file *f = f_info->file;
  if (f == NULL) {
    lock_release(&file_lock);
    unpin(buffer, buffer + size);
    return 0;
  }
  
  int write_bytes = file_write(f, buffer, size);
  lock_release(&file_lock);
  unpin(buffer, buffer + size);
  return write_bytes;
}

void seek(int fd, unsigned position) {
  struct file_info *f_info = search(&thread_current()->file_list, fd);
  struct file *f = f_info->file;
  if (f == NULL)
    exit(-1);
  file_seek(f, position);
}

unsigned tell(int fd) {
  struct file_info *f_info = search(&thread_current()->file_list, fd);
  struct file *f = f_info->file;
  if (f == NULL)
    exit(-1);
  return file_tell(f);
}

void close(int fd) {
  struct file_info *f_info = search(&thread_current()->file_list, fd);
  struct file *f = f_info->file;
  if (f == NULL)
    exit(-1);
  file_close(f);
  list_remove(&f_info->elem);
  free(f_info);
}

int mmap(int fd, void *addr) {
  struct mmap_file *mmap_file;
  struct file_info *f_info;
  struct file *f;
  size_t ofs;
  size_t read_bytes;
  
  if (addr == NULL || is_kernel_vaddr(addr) || pg_round_down (addr) != addr)
    return -1;

  mmap_file = (struct mmap_file *)malloc(sizeof(struct mmap_file));
  if (mmap_file == NULL)
    return -1;
  memset(mmap_file, 0, sizeof(struct mmap_file));
  list_init(&mmap_file->spte_list);
  
  f_info = search(&thread_current()->file_list, fd);
  f = f_info->file;
  if (f == NULL || f_info->fd == 0 || f_info->fd == 1)
    return -1;
  
  mmap_file->file = file_reopen(f);
  mmap_file->map_id = thread_current()->map_id_count;
  thread_current()->map_id_count += 1;
  list_push_back(&thread_current()->mmap_list, &mmap_file->elem);

  ofs = 0;
  read_bytes = file_length(mmap_file->file);
  if (read_bytes == 0)
    return -1;
    
  while (read_bytes > 0) {
    size_t page_read_bytes;
    struct page *spte;
    
    if (find_spte(addr) != NULL)
      return -1;

    page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;

    spte = (struct page *)malloc(sizeof(struct page));
    if (spte == NULL)
      return -1;
    memset(spte, 0, sizeof(struct page));
    spte->type = VM_FILE;
    spte->vaddr = addr;
    spte->write_enable = true;
    spte->file = mmap_file->file;
    spte->offset = ofs;
    spte->read_bytes = page_read_bytes;
    spte->zero_bytes = PGSIZE - page_read_bytes;
    insert_page(&thread_current()->spt, spte);
    list_push_back(&mmap_file->spte_list, &spte->mmap_elem);

    read_bytes -= page_read_bytes;
    addr += PGSIZE;
    ofs += page_read_bytes;
  }
  return mmap_file->map_id;
}

void munmap(mapid_t map_id) {
  struct mmap_file *mmap_file;
  struct list_elem *e;
  
  mmap_file = find_mmap_file(map_id);
  if (mmap_file == NULL)
    return;
  
  e = list_begin(&mmap_file->spte_list);
  while (e != list_end(&mmap_file->spte_list)) {
    struct page *spte = list_entry(e, struct page, mmap_elem);
    if (spte->is_loaded && pagedir_is_dirty(thread_current()->pagedir, spte->vaddr)) {
      lock_acquire(&file_lock);
      file_write_at(spte->file, spte->vaddr, spte->read_bytes, spte->offset);
      lock_release(&file_lock);
      free_frame(pagedir_get_page(thread_current()->pagedir, spte->vaddr));
    }
    spte->is_loaded = false;
    e = list_remove(e);
    delete_page(&thread_current()->spt, spte);
  }

  list_remove(&mmap_file->elem);
  free(mmap_file);
}

struct mmap_file *find_mmap_file(int map_id) {
  struct thread *t = thread_current();
  for (struct list_elem *e = list_begin(&t->mmap_list); e != list_end(&t->mmap_list); e = list_next(e)) {
    struct mmap_file *f = list_entry(e, struct mmap_file, elem);
    if (f->map_id == map_id)
      return f;
  }
  return NULL;
}

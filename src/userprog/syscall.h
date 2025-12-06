#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/thread.h"
#include "lib/user/syscall.h"
#include "vm/page.h"

void syscall_init (void);
void halt(void);
void exit(int status);
pid_t exec(const char *cmdline);
int wait(pid_t pid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
int mmap(int fd, void *addr);
void munmap(mapid_t map_id);
struct mmap_file *find_mmap_file(int map_id);
void close_files(struct list *file_list);

#endif /* userprog/syscall.h */

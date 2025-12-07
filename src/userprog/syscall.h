#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/thread.h"
#include "lib/user/syscall.h"
#ifdef VM
#include "vm/page.h"
#endif

struct file_info
{
  struct file *file;
  int fd;
  struct list_elem elem;
};

struct file_info* search(struct list* file_list, int fd);
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
void close_files(struct list *file_list);
int mmap(int fd, void *addr);  //파일의 특정 구간을 프로세스의 가상 메모리 주소와 연결하는 함수
void munmap(mapid_t map_id);  //mmap된 영역을 해제하는 시스템 콜
struct mmap_file *find_mmap_file(int map_id);  //주어진 map_id에 해당하는 mmap 영역을 mmap_list에서 찾는 함수

#endif /* userprog/syscall.h */

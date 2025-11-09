#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "lib/user/syscall.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"

struct file_info  //한 스레드가 연 파일 하나를 나타내는 구조체
{
  struct file *file;  //파일 객체 포인터
  int fd;  //파일 디스크립터
  struct list_elem elem;  //스레드의 파일 리스트에 연결하기 위한 리스트 노드
};

struct file_info* search(struct list* file_list, int fd);  //파일 리스트에서 주어진 fd 번호를 가진 file_info를 찾아 반환하는 함수
void close_files(struct list *file_list);  //스레드가 연 모든 파일을 닫는 함수
void syscall_init (void);
static void syscall_handler (struct intr_frame *);
void halt(void);  //핀토스 종료 함수
void exit(int status);  //현재 스레드를 종료하는 함수
pid_t exec(const char *cmdline);  //새 사용자 프로그램을 실행하고 자식 프로세스의 pid를 반환하는 함수
int wait(pid_t pid);  //자식 프로세스가 종료될 때까지 기다리고 자식의 종료 상태를 반환하는 함수
bool create(const char *file, unsigned int size);  //새 파일을 생성하는 함수
bool remove(const char *file);  //파일을 제거하는 함수
int open(const char *file);  //파일을 여는 함수
int filesize(int fd);  //파일 크기를 반환하는 함수
int read(int fd, void *buf, unsigned int size);  //버퍼에 파일 내용을 읽어오는 함수
int write(int fd, const void *buf, unsigned int size);  //파일에 버퍼 내용을 적는 함수
void seek(int fd, unsigned int pos);  //파일 오프셋을 옮기는 함수
unsigned int tell(int fd);  //현재 파일 오프셋을 반환하는 함수
void close(int fd);  //파일을 닫는 함수

#endif /* userprog/syscall.h */

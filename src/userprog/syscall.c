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

static void syscall_handler(struct intr_frame *);
void pin(char *start, char *end);
void unpin(char *start, char *end);
bool handle_page_fault(struct page *spte);
struct page *find_spte(void *vaddr);

struct lock file_lock;

struct file_info *search(struct list *file_list, int fd)
{
  struct list_elem *e = list_begin(file_list);
  while (e != list_end(file_list))
  {
    struct file_info *f = list_entry(e, struct file_info, elem);
    if (f->fd == fd)
      return f;
    e = list_next(e);
  }
  return NULL;
}


void close_files(struct list *file_list)
{
  struct list_elem *e;
  while (!list_empty(file_list))
  {
    e = list_pop_front(file_list);
    struct file_info *f = list_entry(e, struct file_info, elem);
    file_close(f->file);
    list_remove(e);
    free(f);
  }
}

void syscall_init(void)
{
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_lock);
}

static void
syscall_handler(struct intr_frame *f UNUSED)
{
  uint32_t *esp = f->esp;
  int args0, args1, args2;

  if (esp == NULL || is_kernel_vaddr(esp))
  {
    exit(-1);
  }

  switch (*esp)
  {
  case SYS_HALT:
    halt();
    break;

  case SYS_EXIT:
    if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))
    {
      exit(-1);
    }
    args0 = *(esp + 1);
    exit(args0);
    break;

  case SYS_EXEC:
    if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))
    {
      exit(-1);
    }
    args0 = *(esp + 1);
    f->eax = exec((const char *)args0);
    break;

  case SYS_WAIT:
    if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))
    {
      exit(-1);
    }
    args0 = *(esp + 1);
    f->eax = wait((pid_t)args0);
    break;

  case SYS_CREATE:
    if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))
    {
      exit(-1);
    }
    if (esp + 2 == NULL || is_kernel_vaddr(esp + 2))
    {
      exit(-1);
    }
    args0 = *(esp + 1);
    args1 = *(esp + 2);
    f->eax = create((const char *)args0, (unsigned)args1);
    break;

  case SYS_REMOVE:
    if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))
    {
      exit(-1);
    }
    args0 = *(esp + 1);
    f->eax = remove((const char *)args0);
    break;

  case SYS_OPEN:
    if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))
    {
      exit(-1);
    }
    args0 = *(esp + 1);
    f->eax = open((const char *)args0);
    break;

  case SYS_FILESIZE:
    if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))
    {
      exit(-1);
    }
    args0 = *(esp + 1);
    f->eax = filesize((int)args0);
    break;

  case SYS_READ:
    if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))
    {
      exit(-1);
    }
    if (esp + 2 == NULL || is_kernel_vaddr(esp + 2))
    {
      exit(-1);
    }
    if (esp + 3 == NULL || is_kernel_vaddr(esp + 3))
    {
      exit(-1);
    }
    args0 = *(esp + 1);
    args1 = *(esp + 2);
    args2 = *(esp + 3);
    f->eax = read((int)args0, (void *)args1, (unsigned)args2);
    break;

  case SYS_WRITE:
    if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))
    {
      exit(-1);
    }
    if (esp + 2 == NULL || is_kernel_vaddr(esp + 2))
    {
      exit(-1);
    }
    if (esp + 3 == NULL || is_kernel_vaddr(esp + 3))
    {
      exit(-1);
    }
    args0 = *(esp + 1);
    args1 = *(esp + 2);
    args2 = *(esp + 3);
    f->eax = write((int)args0, (const void *)args1, (unsigned)args2);
    break;

  case SYS_SEEK:
    if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))
    {
      exit(-1);
    }
    if (esp + 2 == NULL || is_kernel_vaddr(esp + 2))
    {
      exit(-1);
    }
    args0 = *(esp + 1);
    args1 = *(esp + 2);
    seek((int)args0, (unsigned)args1);
    break;

  case SYS_TELL:
    if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))
    {
      exit(-1);
    }
    args0 = *(esp + 1);
    f->eax = tell((int)args0);
    break;

  case SYS_CLOSE:
    if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))
    {
      exit(-1);
    }
    args0 = *(esp + 1);
    close((int)args0);
    break;

  case SYS_MMAP:
    if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))
    {
      exit(-1);
    }
    if (esp + 2 == NULL || is_kernel_vaddr(esp + 2))
    {
      exit(-1);
    }
    args0 = *(esp + 1);
    args1 = *(esp + 2);
    f->eax = mmap((int)args0, (void *)args1);
    break;

  case SYS_MUNMAP:
    if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))
    {
      exit(-1);
    }
    args0 = *(esp + 1);
    munmap((mapid_t)args0);
    break;
  }
}

void halt()
{
  shutdown_power_off();
}

void exit(int status)
{
  struct thread *t = thread_current();

  printf("%s: exit(%d)\n", t->name, status);

  thread_current()->exit_status = status;

  thread_exit();
}

pid_t exec(const char *cmdline)
{
  return process_execute(cmdline);
}

int wait(pid_t pid)
{
  return process_wait(pid);
}

bool create(const char *file, unsigned initial_size)
{
  if (file == NULL)
    exit(-1);
  return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
  if (file == NULL)
    exit(-1);
  return filesys_remove(file);
}

int open(const char *file)
{
  if (file == NULL || is_kernel_vaddr(file))
  {
    exit(-1);
  }
  lock_acquire(&file_lock);
  struct file *open_file = filesys_open(file);
  if (open_file == NULL)
  {
    lock_release(&file_lock);
    return -1;
  }

  int fd_idx = thread_current()->fd_count;
  if (fd_idx >= 128)
  {
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
  char *i;  //페이지 단위 순회 포인터
  i = start;  //i를 start로 초기화
  while (i < end)  //start부터 end까지 페이지 단위로 반복
  {
    struct page *spte = find_spte(i);  //해당 가상 주소에 대한 SPT 엔트리 조회
    spte->pinned = true;  //페이지를 pinned 상태로 설정하여 evict되지 않도록 함
    if (spte->is_loaded == false)  //아직 물리 메모리에 로드되지 않은 경우
      handle_page_fault(spte);  //lazy load 또는 swap-in 수행하여 페이지 로드
    i += PGSIZE;  //다음 페이지 주소로 이동
  }
}

void unpin(char *start, char *end)
{
  char *i;  //페이지 단위 순회 포인터
  i = start;  //i를 start로 초기화
  while (i < end)  //start부터 end까지 페이지 단위 순회
  {
    find_spte(i)->pinned = false;  //해당 페이지의 pinned 상태를 해제하여 교체 가능하게 함
    i += PGSIZE;  //다음 페이지 주소로 이동
  }
}

int read(int fd, void *buffer, unsigned size)
{
  if (buffer == NULL || is_kernel_vaddr(buffer))  //NULL 또는 커널 주소라면 유저 메모리 아님
  {
    exit(-1);  //잘못된 주소 접근 → 프로세스 종료
  }
  pin(buffer, buffer + size);  //read 동안 buffer의 페이지들을 pinned로 설정하여 evict 방지
  lock_acquire(&file_lock);  //파일 시스템 보호용 락 획득

  if (fd == 0)  //stdin에서 입력 읽기
  {
    unsigned idx = 0;  //읽은 바이트 수 카운터
    char w;  //입력 문자 임시 저장 변수
    while (idx < size)  //size만큼 반복하여 키보드 입력 받기
    {
      w = input_getc();  //키보드에서 1바이트 읽기
      ((char *)buffer)[idx] = w;  //buffer에 저장
      if (w == '\0')  //null 입력이면 입력 종료
        break;
      idx++;  //다음 바이트로 이동
    }
    unpin(buffer, buffer + size);  //buffer 페이지 unpin
    lock_release(&file_lock);  //락 해제
    return idx;  //읽은 바이트 수 반환
  }

  if (fd == 1)  //stdout에 대해 read는 잘못된 요청
  {
    unpin(buffer, buffer + size);  //unpin
    lock_release(&file_lock);  //락 해제
    return -1;  //잘못된 read 호출
  }

  struct file_info *f_info = search(&thread_current()->file_list, fd);  //fd에 해당하는 파일 정보 검색
  struct file *f = f_info->file;  //파일 포인터 획득
  if (f == NULL)  //유효하지 않은 파일
  {
    unpin(buffer, buffer + size);  //unpin
    lock_release(&file_lock);  //락 해제
    return -1;  //실패 반환
  }

  int read_size_byte = file_read(f, buffer, size);  //파일에서 size 바이트 읽기
  unpin(buffer, buffer + size);  //buffer unpin
  lock_release(&file_lock);  //파일 시스템 락 해제
  return read_size_byte;  //실제 읽은 바이트 수 반환
}

int write(int fd, const void *buffer, unsigned size)
{
  if (buffer == NULL || is_kernel_vaddr(buffer))  //buffer가 잘못된 포인터인지 확인
  {
    exit(-1);  //잘못된 포인터 → 종료
  }
  pin(buffer, buffer + size);  //write 동안 buffer 페이지를 pinned로 설정
  lock_acquire(&file_lock);  //파일 락 획득

  if (fd == 1)  //stdout
  {
    putbuf(buffer, size);  //콘솔로 size 바이트 출력
    lock_release(&file_lock);  //락 해제
    unpin(buffer, buffer + size);  //unpin
    return size;  //출력한 바이트 수 반환
  }

  if (fd == 0)  //stdin에 write는 의미 없음
  {
    lock_release(&file_lock);  //락 해제
    unpin(buffer, buffer + size);  //unpin
    return 0;  //0바이트 반환
  }

  struct file_info *f_info = search(&thread_current()->file_list, fd);  //fd → file_info 검색
  struct file *f = f_info->file;  //파일 포인터 획득
  if (f == NULL)  //파일 없음
  {
    lock_release(&file_lock);  //락 해제
    unpin(buffer, buffer + size);  //unpin
    return 0;  //쓰기 실패
  }

  int write_bytes = file_write(f, buffer, size);  //파일에 size만큼 쓰기
  lock_release(&file_lock);  //파일 락 해제
  unpin(buffer, buffer + size);  //buffer unpin
  return write_bytes;  //실제 쓰여진 바이트 수 반환
}


void seek(int fd, unsigned position)
{
  struct file_info *f_info = search(&thread_current()->file_list, fd);
  struct file *f = f_info->file;
  if (f == NULL)
    exit(-1);
  file_seek(f, position);
}

unsigned tell(int fd)
{
  struct file_info *f_info = search(&thread_current()->file_list, fd);
  struct file *f = f_info->file;
  if (f == NULL)
    exit(-1);
  return file_tell(f);
}

void close(int fd)
{
  struct file_info *f_info = search(&thread_current()->file_list, fd);
  struct file *f = f_info->file;
  if (f == NULL)
    exit(-1);
  file_close(f);
  list_remove(&f_info->elem);
  free(f_info);
}

int mmap(int fd, void *addr)
{
  struct mmap_file *mmap_file;  //mmap 영역 정보를 저장할 구조체 포인터
  struct file_info *f_info;  //현재 스레드의 열린 파일 목록에서 fd 조회용
  struct file *f;  //파일 포인터
  size_t ofs;  //파일 내에서의 읽기 시작 오프셋
  size_t read_bytes;  //매핑해야 할 전체 파일 크기

  if (addr == NULL || is_kernel_vaddr(addr) || pg_round_down(addr) != addr)  //주소 NULL, 커널 주소, 페이지 정렬 실패 시
    return -1;  //잘못된 주소로 mmap 실패

  mmap_file = (struct mmap_file *)malloc(sizeof(struct mmap_file));  //mmap_file 구조체 동적 할당
  if (mmap_file == NULL)  //할당 실패 시
    return -1;  //mmap 실패
  memset(mmap_file, 0, sizeof(struct mmap_file));  //구조체 메모리 초기화
  list_init(&mmap_file->spte_list);  //이 mmap 영역에 속한 페이지 목록 초기화

  f_info = search(&thread_current()->file_list, fd);  //fd로 열린 파일 정보 조회
  f = f_info->file;  //파일 포인터 저장
  if (f == NULL || f_info->fd == 0 || f_info->fd == 1)  //stdin, stdout, 잘못된 파일이면
    return -1;  //mmap 금지

  mmap_file->file = file_reopen(f);  //파일을 독립적으로 reopen 하여 공유 문제 방지
  mmap_file->map_id = thread_current()->map_id_count;  //해당 mmap의 고유 ID 설정
  thread_current()->map_id_count += 1;  //map_id 증가
  list_push_back(&thread_current()->mmap_list, &mmap_file->elem);  //스레드의 mmap_list에 추가

  ofs = 0;  //파일 읽기 시작 offset
  read_bytes = file_length(mmap_file->file);  //파일 전체 크기 계산
  if (read_bytes == 0)  //파일 크기 0이면
    return -1;  //매핑 의미 없음 → 실패

  while (read_bytes > 0)  //파일 전체가 매핑될 때까지 반복
  {
    size_t page_read_bytes;  //해당 페이지에서 읽을 바이트 수
    struct page *spte;  //SPT 엔트리용 페이지 구조체

    if (find_spte(addr) != NULL)  //이미 동일 vaddr에 페이지가 존재하면
      return -1;  //중복 매핑 불가

    page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;  //이번 페이지에 들어갈 실제 파일 데이터 크기 결정

    spte = (struct page *)malloc(sizeof(struct page));  //SPT 엔트리용 page 생성
    if (spte == NULL)  //메모리 할당 실패 시
      return -1;  //mmap 실패
    memset(spte, 0, sizeof(struct page));  //초기화
    spte->type = VM_FILE;  //파일 기반 페이지임을 설정
    spte->vaddr = addr;  //해당 가상 주소 저장
    spte->write_enable = true;  //mmap 파일은 기본적으로 write 허용
    spte->file = mmap_file->file;  //백킹 스토어 파일 설정
    spte->offset = ofs;  //파일의 읽기 시작 offset
    spte->read_bytes = page_read_bytes;  //읽어야 할 바이트 수
    spte->zero_bytes = PGSIZE - page_read_bytes;  //남는 공간은 0으로 패딩할 바이트 수
    insert_page(&thread_current()->spt, spte);  //SPT에 엔트리 등록
    list_push_back(&mmap_file->spte_list, &spte->mmap_elem);  //mmap 영역의 페이지 목록에 등록

    read_bytes -= page_read_bytes;  //남은 파일 크기 갱신
    addr += PGSIZE;  //다음 페이지 주소로 이동
    ofs += page_read_bytes;  //파일 오프셋 증가
  }
  return mmap_file->map_id;  //mmap 성공 → map_id 반환
}


void munmap(mapid_t map_id)
{
  struct mmap_file *mmap_file;  //해당 map_id의 mmap 영역 구조체
  struct list_elem *e;  //리스트 순회용

  mmap_file = find_mmap_file(map_id);  //map_id로 mmap_file 조회
  if (mmap_file == NULL)  //존재하지 않으면
    return;  //아무 작업도 하지 않음

  e = list_begin(&mmap_file->spte_list);  //해당 mmap 영역의 페이지 리스트 시작점
  while (e != list_end(&mmap_file->spte_list))  //spte_list 끝까지 순회
  {
    struct page *spte = list_entry(e, struct page, mmap_elem);  //리스트 요소를 page 구조체로 변환
    if (spte->is_loaded && pagedir_is_dirty(thread_current()->pagedir, spte->vaddr))  //메모리에 있고 dirty라면
    {
      lock_acquire(&file_lock);  //파일 접근 보호
      file_write_at(spte->file, spte->vaddr, spte->read_bytes, spte->offset);  //변경 내용 파일에 write-back
      lock_release(&file_lock);  //락 해제
      free_frame(pagedir_get_page(thread_current()->pagedir, spte->vaddr));  //프레임 해제
    }
    spte->is_loaded = false;  //메모리에 없음을 표시
    e = list_remove(e);  //리스트에서 해당 엔트리 제거, 다음 엔트리 반환
    delete_page(&thread_current()->spt, spte);  //SPT에서 해당 페이지 삭제 및 메모리 해제
  }

  list_remove(&mmap_file->elem);  //스레드 mmap 리스트에서 mmap_file 제거
  free(mmap_file);  //mmap_file 구조체 해제
}


struct mmap_file *find_mmap_file(int map_id)
{
  struct thread *t = thread_current();  //현재 스레드 포인터 가져오기
  struct list_elem *e = list_begin(&t->mmap_list);  //mmap_list의 첫 요소로 초기화

  while (e != list_end(&t->mmap_list))  //mmap_list 끝까지 순회
  {
    struct mmap_file *f = list_entry(e, struct mmap_file, elem);  //리스트 요소를 mmap_file 구조체로 변환
    if (f->map_id == map_id)  //찾는 map_id와 일치하는 경우
      return f;  //해당 mmap_file 구조체 반환

    e = list_next(e);  //다음 요소로 이동
  }
  return NULL;  //일치하는 map_id가 없으면 NULL 반환
}


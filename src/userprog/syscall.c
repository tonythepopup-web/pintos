#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "vm/frame.h"

struct lock file_lock;  //파일 시스템 접근 때 쓰는 락

struct file_info* search(struct list* file_list, int fd)
{
  struct list_elem *e = list_begin(file_list);  //반복자 선언하고 리스트 첫 원소로 초기화
  while (e != list_end(file_list))  //리스트 마지막 원소까지 반복
  {
    struct file_info *f = list_entry(e, struct file_info, elem);  //파일 정보 받아오기
    if (f->fd == fd)  //파일 디스크립터 맞으면
    {
      return f;  //파일 정보 리턴
    }
    e = list_next(e);  //반복자 업데이트
  }
  return NULL;  //없으면 NULL 반환
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_lock);
}

static void
syscall_handler(struct intr_frame *f)
{
  uint32_t *esp = f->esp;  //유저 스택 포인터 선언
  int args0, args1, args2;  //argument 받을 변수 선언

  if (esp == NULL || is_kernel_vaddr(esp))  //esp가 NULL이거나 포인터가 커널 주소 영역이면
  {
    exit(-1);  //종료
  }

  switch (*esp)  //esp 기준 스위치
  {
    case SYS_HALT:  //esp가 SYS_HALT가 저장된 주소를 가리키고 있으면
    {
      halt();  //정지
      break;  //탈출
    }

    case SYS_EXIT:  //esp가 SYS_EXIT이 저장된 주소를 가리키고 있으면
    {
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))  //esp+1이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      args0 = *(esp + 1);  //status 저장
      exit(args0);  //종료 및 status 반환
      break;
    }

    case SYS_EXEC:  //esp가 SYS_EXEC이 저장된 주소를 가리키고 있으면
    {
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))  //esp+1이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      args0 = *(esp + 1);  //cmdline 저장
      f->eax = exec((const char *)args0);  //cmdline을 exec 함수로 전달
      break;  //탈출
    }

    case SYS_WAIT:  //esp가 SYS_WAIT가 저장된 주소를 가리키고 있으면
    {
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))  //esp+1이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      args0 = *(esp + 1);  //pid 저장
      f->eax = wait((pid_t)args0);  //pid를 wait 함수로 전달
      break;  //탈출
    }

    case SYS_CREATE:  //esp가 SYS_CREATE가 저장된 주소를 가리키고 있으면
    {
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))  //esp+1이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      if (esp + 2 == NULL || is_kernel_vaddr(esp + 2))  //esp+2이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      args0 = *(esp + 1);  //파일 이름 저장
      args1 = *(esp + 2);  //파일 사이즈 저장
      f->eax = create((const char *)args0, (unsigned)args1);  //파일 이름과 사이즈를 create 함수에 전달
      break;  //탈출
    }

    case SYS_REMOVE:  //esp가 SYS_REMOVE가 저장된 주소를 가리키고 있으면
    {
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))  //esp+1이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      args0 = *(esp + 1);  //파일 이름 저장
      f->eax = remove((const char *)args0);  //파일 이름을 remove 함수에 전달
      break;  //탈출
    }

    case SYS_OPEN:  //esp가 SYS_OPEN이 저장된 주소를 가리키고 있으면
    {
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))  //esp+1이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      args0 = *(esp + 1);  //파일 이름 저장
      f->eax = open((const char *)args0);  //파일 이름을 open 함수에 전달
      break;
    }

    case SYS_FILESIZE:  //esp가 SYS_FILESIZE가 저장된 주소를 가리키고 있으면
    {
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))  //esp+1이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      args0 = *(esp + 1);  //파일 디스크립터 저장
      f->eax = filesize((int)args0);  //파일 디스크립터를 filesize 함수에 전달
      break;
    }

    case SYS_READ:  //esp가 SYS_READ가 저장된 주소를 가리키고 있으면
    {
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))  //esp+1이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      if (esp + 2 == NULL || is_kernel_vaddr(esp + 2))  //esp+2이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      if (esp + 3 == NULL || is_kernel_vaddr(esp + 3))  //esp+3이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      args0 = *(esp + 1);  //파일 디스크립터 저장
      args1 = *(esp + 2);  //버퍼 주소 저장
      args2 = *(esp + 3);  //읽을 바이트 수 저장
      f->eax = read((int)args0, (void *)args1, (unsigned)args2);  //파일 디스크립터, 버퍼 주소, 읽을 바이트 수를 read 함수에 전달
      break;  //탈출
    }

    case SYS_WRITE:  //esp가 SYS_WRITE가 저장된 주소를 가리키고 있으면
    {
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))  //esp+1이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      if (esp + 2 == NULL || is_kernel_vaddr(esp + 2))  //esp+2이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      if (esp + 3 == NULL || is_kernel_vaddr(esp + 3))  //esp+3이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      args0 = *(esp + 1);  //파일 디스크립터 저장
      args1 = *(esp + 2);  //버퍼 주소 저장
      args2 = *(esp + 3);  //쓸 바이트 수 저장
      f->eax = write((int)args0, (const void *)args1, (unsigned)args2);  //파일 디스크립터, 버퍼 주소, 쓸 바이트 수를 write 함수에 전달
      break;  //탈출
    }

    case SYS_SEEK:  //esp가 SYS_SEEK이 저장된 주소를 가리키고 있으면
    {
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))  //esp+1이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      if (esp + 2 == NULL || is_kernel_vaddr(esp + 2))  //esp+2이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      args0 = *(esp + 1);  //파일 디스크립터 저장
      args1 = *(esp + 2);  //파일 오프셋을 옮길 위치 저장
      seek((int)args0, (unsigned)args1);  //파일 디스크립터, 파일 오프셋 옮길 위치를 seek 함수에 전달
      break;  //탈출
    }

    case SYS_TELL:  //esp가 SYS_TELL이 저장된 주소를 가리키고 있으면
    {
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))  //esp+1이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      args0 = *(esp + 1);  //파일 디스크립터 저장
      f->eax = tell((int)args0);  //파일 디스크립터를 tell 함수에 전달
      break;  //탈출
    }

    case SYS_CLOSE:  //esp가 SYS_CLOSE가 저장된 주소를 가리키고 있으면
    {
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))  //esp+1이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      args0 = *(esp + 1);  //파일 디스크립터 저장
      close((int)args0);  //파일 디스크립터를 close 함수에 전달
      break;  //탈출
    }

    //Project3

    case SYS_MMAP:  //esp가 SYS_MMAP이 저장된 주소를 가리키고 있으면
    {
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))  //esp+1이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      if (esp + 2 == NULL || is_kernel_vaddr(esp + 2))  //esp+2가 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      args0 = *(esp + 1);  //파일 디스크립터(fd) 저장
      args1 = *(esp + 2);  //매핑 시작 주소(addr) 저장
      f->eax = mmap((int)args0, (void *)args1);  //fd와 addr을 mmap 함수에 전달
      break;  //탈출
    }

    case SYS_MUNMAP:  //esp가 SYS_MUNMAP이 저장된 주소를 가리키고 있으면
    {
      if (esp + 1 == NULL || is_kernel_vaddr(esp + 1))  //esp+1이 NULL이거나 포인터가 커널 주소 영역이면
      {
        exit(-1);  //종료
      }
      args0 = *(esp + 1);  //매핑 ID(mapid) 저장
      munmap((mapid_t)args0);  //mapid를 munmap 함수에 전달
      break;  //탈출
    }
    //
  }
}


void halt()
{
  shutdown_power_off();  //핀토스 종료
}

void exit(int status)
{
  struct thread *t = thread_current();  //스레드 선언
  t->exit_status = status;  //종료 status 저장

  printf("%s: exit(%d)\n", t->name, status);  //종료 메세지 출력

  thread_exit();  //스레드 종료 함수 호출
}

pid_t exec(const char *cmdline)
{
  return process_execute(cmdline);  //process_execute 함수 호출하고 반환값 반환
}

int wait(pid_t pid)
{
  return process_wait(pid);  //process_wait 함수 호출하고 반환값 반환
}

bool create(const char *file, unsigned int size)
{
  if(file==NULL)  //파일 이름이 NULL이면
  {
    exit(-1);  //종료
  }
  return filesys_create(file, size);  //filesys_create 함수 호출하고 반환값 반환
}

bool remove(const char *file)
{
  if(file==NULL)  //파일 이름이 NULL이면
  {
    exit(-1);  //종료
  }
  return filesys_remove(file);  //filesys_remove 함수 호출하고 반환값 반환
}

int open(const char *file)
{
  if (file == NULL || is_kernel_vaddr(file))  //file이 NULL이거나 포인터가 커널 주소 영역이면
  {
    exit(-1);  //종료
  }
  lock_acquire(&file_lock);  //파일 시스템 락 획득
  struct file *f = filesys_open(file);  //실제로 파일을 열고 저장
  if (f == NULL)  //만약 파일이 안 열렸으면
  {
    lock_release(&file_lock);  //락 풀기
    return -1;  //실패 반환
  }

  int fd = thread_current()->fd_count;  //파일 디스크립터 저장
  if (fd >= 128)  //파일 디스크립터가 128보다 크거나 같으면
  {
    file_close(f);  //파일을 닫고
    lock_release(&file_lock);  //락 풀기
    return -1;  //실패 반환
  }
  thread_current()->fd_count += 1;  //성공이면 fd_count를 1 증가
    
  struct file_info *tmp = malloc(sizeof(struct file_info *));  //file_info 구조체를 동적 할당
  tmp->file = f;  //파일 정보에 파일 이름을 저장
  tmp->fd = fd;  //파일 정보에 파일 디스크립터를 저장
  list_push_back(&thread_current()->file_list, &tmp->elem);  //지금 수정한 파일 정보를 현재 스레드의 파일 리스트에 추가
  lock_release(&file_lock);  //락 풀기
  return fd;  //아까 +1 해둔 파일 디스크립터를 반환
}

int filesize(int fd)
{
  struct file_info *f_info = search(&thread_current()->file_list, fd);  //현재 스레드의 파일 리스트에서 파일 디스크립터가 일치하는 파일을 찾아서 그 파일의 파일 정보 저장
  if (f_info->file == NULL)  //파일 포인터가 NULL이면
  {
    exit(-1);  //종료
  }
  return file_length(f_info->file);  //파일의 총 바이트 수를 반환
}

//Project3

int read(int fd, void *buf, unsigned int size)
{
  if (buf == NULL || is_kernel_vaddr(buf))  //buf가 NULL이거나 포인터가 커널 주소 영역이면
  {
    exit(-1);  //종료
  }
  //Project3
  pin((char *)buf, (char *)buf + size);
  //
  lock_acquire(&file_lock);  //파일 시스템 락 획득
  if (fd == 0)  //파일 디스크립터가 0이면(stdin이면)
  {
    char c;  //문자를 임시로 저장하는 변수
    unsigned int i = 0;  //반복자
    while (i < size)  //반복자가 읽을 총 바이트 수보다 작은 동안 반복
    {
      c = input_getc();  //키보드 인터럽트 핸들러가 입력 문자를 버퍼에 넣는 함수(devices/input.c에 있음)
      ((char *)buf)[i] = c;  //버퍼의 i번째 칸에 임시로 저장해둔 문자 저장
      if (c == '\0')  //널 문자가 들어오면
      {
        break;  //탈출
      }
      i++;  //반복자 업데이트
    }
    lock_release(&file_lock);  //락 풀기
    //Project3
    unpin((char *)buf, (char *)buf + size);
    //
    return i;  //읽은 바이트 수 반환
  }
  if (fd == 1)  //파일 디스크립터가 1이면(stdout이면)
  {
    lock_release(&file_lock);  //락 풀기
    //Project3
    unpin((char *)buf, (char *)buf + size);
    //
    return -1;  //읽기는 실패이므로 실패 반환
  }

  struct file_info *f_info = search(&thread_current()->file_list, fd);  //현재 스레드의 파일 리스트에서 파일 디스크립터가 일치하는 파일을 찾아서 그 파일의 파일 정보 저장
  if (f_info->file == NULL)  //파일 포인터가 NULL이면
  {
    lock_release(&file_lock);  //락 풀기
        //Project3
    unpin((char *)buf, (char *)buf + size);
    //
    return -1;  //실패 반환
  }

  int read_byte = file_read(f_info->file, buf, size);  //파일에서 size만큼 읽어서 buf로 복사
  lock_release(&file_lock);  //락 풀기
      //Project3
  unpin((char *)buf, (char *)buf + size);
    //
  return read_byte;  //읽은 바이트 수 반환
}

int write(int fd, const void *buf, unsigned int size)
{
  if (buf == NULL || is_kernel_vaddr(buf))  //buf가 NULL이거나 포인터가 커널 주소 영역이면
  {
    exit(-1);  //종료
  }
  
  pin((char *)buf, (char *)buf + size);

  lock_acquire(&file_lock);  //파일 시스템 락 획득
  if (fd == 0)  //파일 디스크립터가 0이면(stdin이면)
  {
    lock_release(&file_lock);  //락 풀기
    unpin((char *)buf, (char *)buf + size);
    return 0;  //아무것도 안 썼으니까 0 반환
  }
  if (fd == 1)  //파일 디스크립터가 1이면(stdout이면)
  {
    putbuf(buf, size);  //콘솔에 출력
    lock_release(&file_lock);  //락 풀기
    unpin((char *)buf, (char *)buf + size);
    return size;  //적은 바이트 수 반환
  }

  struct file_info *f_info = search(&thread_current()->file_list, fd);  //현재 스레드의 파일 리스트에서 파일 디스크립터가 일치하는 파일을 찾아서 그 파일의 파일 정보 저장
  if (f_info->file == NULL)  //파일 포인터가 NULL이면
  {
    lock_release(&file_lock);  //락 풀기
    unpin((char *)buf, (char *)buf + size);
    return 0;  //아무것도 안 썼으니까 0 반환
  }

  int write_byte = file_write(f_info->file, buf, size);  //buf에서 size만큼 복사해서 파일에 쓰기
  lock_release(&file_lock);  //락 풀기
  unpin((char *)buf, (char *)buf + size);
  return write_byte;  //적은 바이트 수 반환
}

void seek(int fd, unsigned int pos)
{
  struct file_info *f_info = search(&thread_current()->file_list, fd);  //현재 스레드의 파일 리스트에서 파일 디스크립터가 일치하는 파일을 찾아서 그 파일의 파일 정보 저장
  if (f_info->file == NULL)  //파일 포인터가 NULL이면
  {
    exit(-1);  //종료
  }
  file_seek(f_info->file, pos);  //파일 오프셋을 pos 바이트 지점으로 이동
}

unsigned int tell(int fd)
{
  struct file_info *f_info = search(&thread_current()->file_list, fd);  //현재 스레드의 파일 리스트에서 파일 디스크립터가 일치하는 파일을 찾아서 그 파일의 파일 정보 저장
  if (f_info->file == NULL)  //파일 포인터가 NULL이면
  {
    exit(-1);  //종료
  }
  return file_tell(f_info->file);  //현재 파일의 파일 오프셋 반환
}

void close(int fd)
{
  struct file_info *f_info = search(&thread_current()->file_list, fd);  //현재 스레드의 파일 리스트에서 파일 디스크립터가 일치하는 파일을 찾아서 그 파일의 파일 정보 저장
  if (f_info->file == NULL)  //파일 포인터가 NULL이면
  {
    exit(-1);  //종료
  }
  file_close(f_info->file);  //파일 닫기
  list_remove(&f_info->elem);  //현재 스레드의 파일 리스트에서 이 파일 정보 노드 제거
  free(f_info);  //파일 정보 구조체 동적 할당 해제
}

//Project3

void pin(char *start, char *end)
{
  for (char *i = start; i < end; i += PGSIZE) {
    struct page *spte = find_spte(i);
    spte->pinned = true;
    if (spte->is_loaded == false)
      handle_page_fault(spte);
  }
}

void unpin(char *start, char *end)
{
  for (char *i = start; i < end; i += PGSIZE)
    find_spte(i)->pinned = false;
}

// 성공 시 map_id 리턴, 실패 시 -1 리턴
int mmap(int fd, void *addr) {
  // addr 시작점이 page 단위 정렬 안 되었을 경우 page 단위로 접근 불가함
  if (addr == NULL || is_kernel_vaddr(addr) || pg_round_down (addr) != addr)
    return -1;

  // memory mapping할 파일 탐색
  struct mmap_file *mmap_file = (struct mmap_file *)malloc(sizeof(struct mmap_file));
  if (mmap_file == NULL)
    return -1;
  memset(mmap_file, 0, sizeof(struct mmap_file));
  list_init(&mmap_file->spte_list);
  struct file_info *f_info = search(&thread_current()->file_list, fd);
  struct file *f = f_info->file;
  if (f == NULL || f_info->fd == 0 || f_info->fd == 1)
    return -1;
  
  // 현재 thread의 mmap_list에 mmap file 추가
  mmap_file->file = file_reopen(f);
  mmap_file->map_id = thread_current()->map_id_count;
  thread_current()->map_id_count += 1;
  list_push_back(&thread_current()->mmap_list, &mmap_file->elem);

  // file을 메모리로 load
  size_t ofs = 0;
  size_t read_bytes = file_length(mmap_file->file);
  if (read_bytes == 0)
    return -1;
  //size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;
  while (read_bytes > 0) {
    if (find_spte(addr) != NULL)
      return -1;

    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    //size_t page_zero_bytes = PGSIZE - page_read_bytes;

    struct page *spte = (struct page *)malloc(sizeof(struct page));
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

    /* Advance. */
    read_bytes -= page_read_bytes;
    //zero_bytes -= page_zero_bytes;
    addr += PGSIZE;
    ofs += page_read_bytes;
  }
  return mmap_file->map_id;
}

void munmap(mapid_t map_id) {
  struct mmap_file *mmap_file = find_mmap_file(map_id);
  if (mmap_file == NULL)
    return;
  
  // mmap_file의 spte_list에 존재하는 모든 spte 제거
  // spte가 물리 페이지에 존재하고, dirty한 경우 disk에 기록
  struct list_elem *e = list_begin(&mmap_file->spte_list);
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

// 현재 thread의 mmap_list에서 map_id에 해당하는 mmap file 찾아서 리턴
struct mmap_file *find_mmap_file(int map_id) {
  struct thread *t = thread_current();
  for (struct list_elem *e = list_begin(&t->mmap_list); e != list_end(&t->mmap_list); e = list_next(e)) {
    struct mmap_file *f = list_entry(e, struct mmap_file, elem);
    if (f->map_id == map_id)
      return f;
  }
  return NULL;
}
//
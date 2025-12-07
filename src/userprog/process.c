#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/page.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static bool install_page (void *upage, void *kpage, bool writable);
void argument_passing(int argc, char **argv, struct intr_frame *_if);
bool handle_page_fault (struct page *spte);
bool stack_growth(void* addr);
bool load_file (void *kaddr, struct page *spte);
extern struct lock file_lock;

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL) {
    palloc_free_page(fn_copy);
    return TID_ERROR;
  }
  strlcpy (fn_copy, file_name, PGSIZE);

  char *ret_ptr, *save_ptr;
  ret_ptr = palloc_get_page(0);
  strlcpy(ret_ptr, fn_copy, PGSIZE);
  ret_ptr = strtok_r(ret_ptr, " ", &save_ptr);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (ret_ptr, PRI_DEFAULT, start_process, fn_copy);
  palloc_free_page(ret_ptr);
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy);

  sema_down(&(thread_current()->load_sema));

  for (struct list_elem* e = list_begin(&(thread_current()->child_list)); e != list_end(&(thread_current()->child_list)); e = list_next(e))
  {
    struct thread* thr = list_entry(e, struct thread, child_elem);
    if (thr->exit_status == -1) {
      return process_wait (tid);
    }
  }
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  int argc = 0;
  char *argv[128];
  char *ret_ptr, *save_ptr;

  ret_ptr = strtok_r(file_name, " ", &save_ptr);
  
  while(ret_ptr != NULL){
    argv[argc] = ret_ptr;
    argc++;
    ret_ptr = strtok_r(NULL, " ", &save_ptr);
  }

  page_init(&thread_current()->spt);  //실행되는 스레드의 SPT 초기화

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);
  
  if (success)
    argument_passing(argc, argv, &if_);

  palloc_free_page(file_name);
  sema_up(&(thread_current()->parent->load_sema));
  if (!success) {
    exit(-1);
  }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

void argument_passing(int argc, char **argv, struct intr_frame *_if){

  for(int i = argc - 1; i >= 0; i--){
    _if->esp -= ((int)strlen(argv[i]) + 1);
    memcpy(_if->esp, argv[i], (int)strlen(argv[i]) + 1);
    argv[i] = (char*)_if->esp;
  }
  
  _if->esp -= ((unsigned int)_if->esp % 4 + 4);
  memset(_if->esp, 0, (unsigned int)_if->esp % 4 + 4);

  for(int i = argc - 1; i >= 0; i--){
    _if->esp -= 4;
    *(char**)_if->esp = argv[i];
  }

  _if->esp -= 4;
  *(char**)_if->esp = _if->esp + 4;

  _if->esp -= 4;
  *(int*)_if->esp = argc;

  _if->esp -= 4;
  memset(_if->esp, 0, 4);
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{
  struct thread *cur = thread_current();
  int exit_status = -1;
  struct list_elem *e = list_begin(&(cur->child_list));
  while (e != list_end(&(cur->child_list))) {
    struct thread *thr = list_entry(e, struct thread, child_elem);
    if (thr->tid == child_tid) {
      sema_down(&(thr->child_sema));
      list_remove(&(thr->child_elem));
      exit_status = thr->exit_status;
      sema_up(&(thr->exit_sema));
      break;
    }
    e = list_next(e);
  }
  return exit_status;
}


/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  sema_up(&(cur->child_sema));
  
  int map_id = 1;  //첫 mmap 영역의 ID는 1부터 시작
  while (map_id < cur->map_id_count)  //모든 mmap된 파일에 대해 unmap 수행
  {
    munmap(map_id);  //map_id에 해당하는 mmap 영역 해제
    map_id++;  //다음 mmap 영역으로 이동
  }
  
  close_files(&cur->file_list);
  file_close(cur->running_file);
  page_destroy(&cur->spt);  //SPT의 모든 페이지 엔트리 및 프레임 정리

  sema_down(&(cur->exit_sema));

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  lock_acquire (&file_lock);

  file = filesys_open (file_name);
  if (file == NULL) 
    {
      lock_release (&file_lock);
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  t->running_file = file;
  file_deny_write(t->running_file);
  
  lock_release (&file_lock);

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);  //read_bytes + zero_bytes는 반드시 페이지 단위여야 함
  ASSERT (pg_ofs (upage) == 0);  //upage는 페이지 정렬되어 있어야 함
  ASSERT (ofs % PGSIZE == 0);  //파일 오프셋 역시 페이지 정렬 필요

  file_seek (file, ofs);  //파일 읽기 시작 위치를 ofs로 이동
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;  //이번 페이지에서 읽을 바이트 수 계산
      size_t page_zero_bytes = PGSIZE - page_read_bytes;  //남은 공간은 0으로 채울 바이트 수

      /* Get a page of memory. */
      //uint8_t *kpage = palloc_get_page (PAL_USER);
      //if (kpage == NULL)
      //  return false;

      /* Load this page. */
      //if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
      //  {
      //    palloc_free_page (kpage);
      //    return false; 
      //  }
      //memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      //if (!install_page (upage, kpage, writable)) 
      //  {
      //    palloc_free_page (kpage);
      //    return false; 
      //  }

      struct page *spte = (struct page *)malloc(sizeof(struct page));  //SPT 엔트리 할당
      if (spte == NULL)  //할당 실패 시
        return false;
      memset(spte, 0, sizeof(struct page));  //SPT 엔트리 초기화
      spte->type = VM_BIN;  //실행 파일에서 로드되는 페이지
      spte->vaddr = upage;  //해당 페이지의 유저 가상 주소
      spte->write_enable = writable;  //쓰기 가능 여부 설정
      spte->file = file;  //백킹 스토어 파일
      spte->offset = ofs;  //파일 내에서 읽기 시작 위치
      spte->read_bytes = page_read_bytes;  //로드할 바이트 수
      spte->zero_bytes = page_zero_bytes;  //0으로 채울 바이트 수
      insert_page(&thread_current()->spt, spte);  //SPT에 등록

      /* Advance. */
      read_bytes -= page_read_bytes;  //남은 읽기 바이트 감소
      zero_bytes -= page_zero_bytes;  //남은 zero 바이트 감소
      upage += PGSIZE;  //다음 유저 페이지 주소로 이동
      ofs += page_read_bytes;  //파일 오프셋 증가
    }
  return true;  //세그먼트 로드 성공
}


/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  struct page *spte = (struct page *)malloc(sizeof(struct page));  //스택 페이지용 SPT 엔트리 생성
  if (spte == NULL)  //SPT 엔트리 생성 실패 시
    return false;

  struct frame *kpage = alloc_frame (PAL_USER | PAL_ZERO);  //유저용 페이지 + 0으로 초기화된 프레임 요청
  if (kpage != NULL) 
    {
      kpage->spte = spte;  //프레임이 어떤 페이지에 대응하는지 연결
      bool success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage->kaddr, true);  //가상 주소와 물리 프레임 매핑
      if (success)  //매핑 성공 시
        *esp = PHYS_BASE;  //스택 포인터를 최상단(PHYS_BASE)으로 설정
      else {
        free_frame(kpage->kaddr);  //프레임 반환
        free(spte);  //SPT 엔트리 해제
        return success;  //false 반환
      }
    }
  
  memset(spte, 0, sizeof(struct page));  //SPT 엔트리 초기화
  spte->type = VM_ANON;  //스택은 익명 페이지(anon)
  spte->vaddr = ((uint8_t *) PHYS_BASE) - PGSIZE;  //스택이 매핑되는 유저 가상 주소
  spte->write_enable = true;  //스택은 항상 쓰기 가능
  spte->is_loaded = true;  //이미 프레임이 존재하므로 loaded = true
  insert_page(&thread_current()->spt, spte);  //SPT에 스택 페이지 등록

  return true;  //스택 설정 성공
}


/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

bool handle_page_fault (struct page *spte) {
  bool success;  //파일 로드, 매핑 설치 등 성공 여부 저장
  struct frame *kpage = alloc_frame(PAL_USER);  //유저용 프레임 할당 (필요 시 페이지 교체 수행)
  kpage->spte = spte;  //프레임과 해당 SPT 엔트리를 연결

  switch(spte->type) {  //페이지 타입에 따라 처리 방식 분기
    case VM_BIN:  //실행 파일에서 가져오는 페이지
    case VM_FILE:  //mmap 파일에서 가져오는 페이지
      success = load_file(kpage->kaddr, spte);  //파일에서 페이지 내용 읽어오기
      if (!success) {  //파일 로딩 실패 시
        free_frame(kpage->kaddr);  //할당한 프레임 해제
        return false;  //페이지 폴트 처리 실패
      }
      memset(kpage->kaddr + spte->read_bytes, 0, spte->zero_bytes);  //남는 부분은 0으로 초기화
      success = install_page(spte->vaddr, kpage->kaddr, spte->write_enable);  //가상주소와 프레임 매핑
      if (!success) {  //매핑 실패 시
        free_frame(kpage->kaddr);  //프레임 해제
        return false;  //실패 반환
      }
      spte->is_loaded = true;  //페이지가 메모리에 적재됨을 기록
      return true;  //성공적으로 페이지 폴트 처리 완료

    case VM_ANON:  //익명 페이지 (스택, 힙, swap-out된 페이지 등)
      swap_in(spte->swap_table, kpage->kaddr);  //swap 영역에서 프레임으로 데이터 불러오기
      success = install_page(spte->vaddr, kpage->kaddr, spte->write_enable);  //가상주소와 프레임 매핑
      if (!success) {  //매핑 실패 시
        free_frame(kpage->kaddr);  //프레임 해제
        return false;  //실패 반환
      }
      spte->is_loaded = true;  //메모리에 정상적으로 올라온 상태 표시
      return true;  //성공

    default:  //정의되지 않은 타입
      break;  //처리 불가
  }
  return false;  //페이지 폴트 처리 실패
}


bool stack_growth(void* addr){
  struct frame* frame;  //새로 확장할 스택 페이지에 대응하는 프레임 포인터
  struct page* spte;  //SPT 엔트리를 저장할 포인터
  
  if(addr < PHYS_BASE - 2048 * PGSIZE)  //스택 최대 크기(약 8MB)를 초과하면 확장 금지
  {
    return false;  //잘못된 스택 접근
  }

  while (!find_spte(addr)) {  //해당 addr에 SPT 엔트리가 없으면 스택 페이지를 확장해야 함
    frame = alloc_frame(PAL_USER | PAL_ZERO);  //0으로 초기화된 유저용 프레임 할당
    if(!frame){  //프레임 할당 실패 시
      return false;  //스택 확장 실패
    }

    spte = malloc(sizeof(struct page));  //새로운 스택 페이지에 대한 SPT 엔트리 생성
    frame->spte = spte;  //프레임과 페이지 엔트리 연결
    
    if(!install_page(pg_round_down(addr), frame->kaddr, true)) {  //가상주소와 물리 프레임 매핑
      free_frame(frame->kaddr);  //매핑 실패 시 프레임 반환
      return false;  //확장 실패
    }

    spte->type = VM_ANON;  //스택은 익명 페이지 유형(VM_ANON)
    spte->vaddr = pg_round_down(addr);  //스택 페이지의 가상 주소 저장
    spte->write_enable = true;  //스택은 항상 쓰기 가능
    spte->is_loaded = true;  //프레임이 이미 매핑되었으므로 메모리에 로드됨

    if(!insert_page(&(frame->t->spt), spte)){  //현재 스레드의 SPT에 새로운 엔트리 삽입
      return false;  //SPT 삽입 실패
    }

    addr += PGSIZE;  //다음 스택 페이지를 검사하기 위해 주소 증가
  }
  return true;  //스택 확장 완료
}



#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;
  char *ret, *save;  //파싱에 사용할 임시 버퍼 포인터, 재진입 가능한 파서의 상태 저장 포인터

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
  {
    return TID_ERROR;
  }
  strlcpy (fn_copy, file_name, PGSIZE);

  ret=palloc_get_page(0);  //파싱을 위해 별도의 페이지 버퍼 ret도 하나 더 할당
  strlcpy(ret, fn_copy, PGSIZE);  //방금 복사한 커맨드라인을 ret으로 다시 복사
  ret=strtok_r(ret, " ", &save);  //string.c에 있는 함수 사용  //공백을 구분자로 해서 첫 토큰을 ret이 가리키게 함

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (ret, PRI_DEFAULT, start_process, fn_copy);
  palloc_free_page(ret);  //파싱용 임시 페이지 ret는 더 이상 필요 없으므로 해제
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy);

  sema_down(&(thread_current()->load_sema));  //자식의 로딩 완료를 기다리기 위해 sema_down 

  struct list_elem *e = list_begin(&(thread_current()->child_list));  //반복자를 현재 스레드의 child list의 첫번재 노드로 설정
  while (e != list_end(&(thread_current()->child_list)))  //반복자가 child list의 마지막 노드가 될 때까지 반복
  {
    struct thread *t = list_entry(e, struct thread, child_elem);  //현재 자식 스레드 저장
    if (t->exit_status == -1)  //로딩 실패한 경우
    {
      return process_wait(tid);  //자식을 기다리고 종료 코드 반환
    }
    e = list_next(e);  //반복자 업데이트
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
  char *argv[128];  //토큰 포인터 담을 배열
  char *ret, *save;  //파싱에 사용할 임시 버퍼 포인터, 재진입 가능한 파서의 상태 저장 포인터

  ret=strtok_r(file_name, " ", &save);  //공백을 구분자로 해서 첫 토큰을 ret이 가리키게 함
  int i=0;  //반복자 초기화
  while(ret&&i<127)  //ret이 NULL이 아니고 반복자가 127 이전까지 반복  //128 아니고 127인 이유는 마지막 칸에는 NULL 들어가야 하기 때문
  {
    argv[i++]=ret;  //argv에 ret 저장하고 반복자 +1
    ret=strtok_r(NULL, " ", &save);  //공백을 구분자로 해서 첫 토큰을 ret이 가리키게 함
  }
  argv[i] = NULL;  //마지막 칸에 NULL

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);
  if(success)  //load 성공하면
  {
    argument_passing(i, argv, &if_);  //문자열, 포인터, 리턴 주소, argc, argv를 표준 레이아웃에 맞게 유저 스택에 push
  }

  /* If load failed, quit. */
  palloc_free_page (file_name);
  sema_up(&(thread_current()->parent->load_sema));  //자식이 로딩 완료했으므로 sema_up 
  if (!success)  //실패면
    exit (-1);  //종료

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

void argument_passing(int argc, char **argv, struct intr_frame *_if)
{
  for(int i=argc-1; i>=0; i--)  //스택은 낮은 주소로 자라므로 역순으로 순회
  {
    _if->esp-=((int)strlen(argv[i])+1);  //문자열의 길이+널 문자 자리만큼 스택 포인터 이동
    memcpy(_if->esp, argv[i], (int)strlen(argv[i])+1);  //스택 포인터가 이동하며 생긴 자리에 문자열+널 문자 복사
    argv[i]=(char*)_if->esp;  //이제 argv[i]는 커널 버퍼 주소가 아니라 유저 스택 내 문자열의 시작 주소를 가리키게 됨
  }

  _if->esp-=((uintptr_t)_if->esp%4+4);  //esp가 4바이트 경계에 맞춰지도록 패딩을 까는데, pdf 예시를 따라서 4만큼 더 비움
  memset(_if->esp, 0, (uintptr_t)_if->esp%4+4);  //빈자리에 0 채우기

  for(int i=argc-1; i>=0; i--)  //스택은 낮은 주소로 자라므로 역순으로 순회
  {
    _if->esp-=4;  //포인터 한 칸 분량 자리 만들기
    *(char**)_if->esp=argv[i];  //그 자리에 문자열 주소 저장
  }

  _if->esp-=4;  //포인터 한 칸 분량 자리 만들기
  *(char***)_if->esp=(char**)(_if->esp+4);  //argv 자체 주소(포인터 배열의 시작 주소) 저장

  _if->esp-=4;  //argc 저장할 자리 만들기
  *(int*)_if->esp=argc;  //argc 저장

  _if->esp-=4;  //리턴 주소 저장할 자리 만들기
  memset(_if->esp, 0, 4);  //가짜 리턴 주소(0) 저장
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
  struct thread *cur = thread_current();  //현재 스레드 저장
  int exit_status = -1;  //종료 상태 변수

  struct list_elem *e = list_begin(&(cur->child_list));  //반복자를 현재 스레드의 child list의 첫번재 노드로 설정
  while (e != list_end(&(cur->child_list)))  //반복자가 child list의 마지막 노드가 될 때까지 반복
  {
    struct thread *t = list_entry(e, struct thread, child_elem);  //현재 자식 스레드 저장
    if (t->tid == child_tid)  //자식 스레드의 tid가 찾던 tid와 일치하면
    {
      sema_down(&(t->child_sema));  //자식 스레드의 exit 시그널 받을 때까지 부모 스레드가 sleep하도록 sema_down
      list_remove(&(t->child_elem));  //자식 리스트에서 제거해서 부모가 다시 이 자식에 대해 wait하는 것을 방지
      exit_status = t->exit_status;  //종료 상태 저장
      sema_up(&(t->exit_sema));  //부모가 자식을 수확했으니 이제 sema_up
      break;  //탈출
    }
    e = list_next(e);  //반복자 업데이트
  }
  return exit_status;  //종료 상태 반환
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

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

  sema_up(&(cur->child_sema));  //자식이 종료되었음을 부모에게 알려주기 위해 sema_up
  file_close(cur->running);  //현재 실행중인 파일 닫기
  struct list *file_list = &cur->file_list;  //현재 스레드의 파일 리스트 저장
  while (!list_empty(file_list))  //파일 리스트가 완전히 빌 때까지 반복
  {
    struct list_elem *e = list_pop_front(file_list);  //리스트 맨 앞 노드를 pop
    struct file_info *f_info = list_entry(e, struct file_info, elem);  //pop된 노드의 파일 정보를 저장
    if (f_info->file != NULL)  //파일 포인터가 NULL이 아니면
    {
      file_close(f_info->file);  //파일 닫기
    }
    free(f_info);  //파일 정보 구조체 할당 해제
  }
  sema_down(&(cur->exit_sema));  //자식은 부모에게 수확될 때까지 sema_down해서 기다림
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
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }
  t->running=file;  //현재 실행 중인 파일 저장
  file_deny_write(t->running);  //실행 중인 파일에 대한 write를 금지해 다른 프로세스나 유저가 실행 중인 파일을 덮어쓰지 못하게 막음

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
  //file_close (file);
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
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
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

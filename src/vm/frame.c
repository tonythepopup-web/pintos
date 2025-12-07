#include "vm/frame.h"
#include "threads/synch.h"
#include "lib/kernel/bitmap.h"
#include "devices/block.h"
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include <string.h>

extern struct lock file_lock;  //파일 시스템 접근 시 동기화를 위한 전역 락

static struct list frame_table;  //모든 프레임 엔트리를 담는 전역 리스트
static struct lock frame_table_lock;  //프레임 테이블 보호용 락
static struct list_elem *clock_hand;  //CLOCK 알고리즘 포인터

static struct bitmap *swap_bitmap;  //swap 슬롯 사용 여부 관리용 비트맵
static struct block *swap_block;  //swap 영역으로 사용할 블록 디바이스
static struct lock swap_lock;  //swap 영역 접근 동기화 락
static size_t swap_size;  //swap 슬롯 총 개수

static struct list_elem *clock_get_next(void);  //CLOCK 포인터를 다음 프레임으로 이동
static void __free_frame(struct frame *f);  //프레임을 실제로 해제하는 내부 함수


void frame_init(void)
{
    list_init(&frame_table);  //프레임 테이블 리스트 초기화
    lock_init(&frame_table_lock);  //프레임 테이블 접근 락 초기화
    clock_hand = NULL;  //CLOCK 포인터 초기 값 설정
}


void swap_init(void)
{
    swap_block = block_get_role(BLOCK_SWAP);  //swap 역할을 가진 블록 디바이스 획득
    swap_size = block_size(swap_block) / 8;  //총 섹터 수 / 8 = swap 슬롯 수 계산
    swap_bitmap = bitmap_create(swap_size);  //swap 슬롯 상태를 저장할 비트맵 생성
    lock_init(&swap_lock);  //swap 영역 동기화를 위한 락 초기화
}


void insert_frame(struct frame *f)
{
    lock_acquire(&frame_table_lock);  //프레임 테이블 접근 보호
    list_push_back(&frame_table, &f->elem);  //프레임 엔트리를 테이블 뒤에 삽입
    lock_release(&frame_table_lock);  //락 해제
}


void delete_frame(struct frame *f)
{
    ASSERT(lock_held_by_current_thread(&frame_table_lock));  //현재 스레드가 락을 보유 중인지 확인

    if (clock_hand == &f->elem)  //CLOCK 포인터가 현재 제거할 프레임을 가리키는 경우
    {
        clock_hand = clock_get_next();  //CLOCK 포인터를 다음 엔트리로 이동
    }
    list_remove(&f->elem);  //프레임 테이블에서 해당 엔트리 제거
}


static struct list_elem *clock_get_next(void)
{
    ASSERT(lock_held_by_current_thread(&frame_table_lock));  //락 보유 여부 확인

    if (list_empty(&frame_table))  //프레임 테이블이 비어 있으면
        return NULL;  //NULL 반환

    if (clock_hand == NULL || clock_hand == list_end(&frame_table))  //초기 상태 또는 리스트 끝이면
    {
        clock_hand = list_begin(&frame_table);  //CLOCK 포인터를 리스트 처음으로 이동
        return clock_hand;  //새 CLOCK 포인터 반환
    }

    clock_hand = list_next(clock_hand);  //다음 엔트리로 이동
    if (clock_hand == list_end(&frame_table))  //리스트 끝이면
        return clock_get_next();  //다시 처음으로 이동

    return clock_hand;  //이동한 CLOCK 포인터 반환
}


struct frame *alloc_frame(enum palloc_flags flag)
{
    struct frame *new_frame = (struct frame *)malloc(sizeof(struct frame));  //frame 구조체 동적 메모리 할당
    if (new_frame == NULL)  //메모리 부족 시
        return NULL;  //NULL 반환

    memset(new_frame, 0, sizeof(struct frame));  //구조체 필드 초기화
    new_frame->t = thread_current();  //프레임을 소유하는 스레드를 기록
    new_frame->kaddr = palloc_get_page(flag);  //물리 페이지(프레임) 요청

    while (new_frame->kaddr == NULL)  //프레임 부족 → 페이지 교체 필요
    {
        struct list_elem *e;  //CLOCK 결과 엔트리
        struct frame *victim;  //희생할 victim frame

        lock_acquire(&frame_table_lock);  //프레임 테이블 보호 락 획득
        e = clock_get_next();  //CLOCK 알고리즘으로 victim 후보 획득
        victim = list_entry(e, struct frame, elem);  //엔트리를 frame 구조체로 변환

        while (victim->spte->pinned ||  //pinned된 페이지는 교체 금지
               pagedir_is_accessed(victim->t->pagedir, victim->spte->vaddr))  //access bit가 1이면 second chance
        {
            pagedir_set_accessed(victim->t->pagedir, victim->spte->vaddr, false);  //access bit 초기화
            e = clock_get_next();  //다음 엔트리로 이동
            victim = list_entry(e, struct frame, elem);  //frame 갱신
        }

        if (victim->spte->type == VM_BIN)  //실행 파일 기반 페이지
        {
            if (pagedir_is_dirty(victim->t->pagedir, victim->spte->vaddr))  //dirty라면
            {
                victim->spte->type = VM_ANON;  //익명 페이지로 타입 변경
                victim->spte->swap_table = swap_out(victim->kaddr);  //swap 영역에 저장
            }
        }
        else if (victim->spte->type == VM_FILE)  //mmap 파일 기반 페이지
        {
            if (pagedir_is_dirty(victim->t->pagedir, victim->spte->vaddr))  //dirty라면
            {
                lock_acquire(&file_lock);  //파일 접근 보호
                file_write_at(victim->spte->file, victim->spte->vaddr,
                              victim->spte->read_bytes, victim->spte->offset);  //파일에 변경 내용 반영
                lock_release(&file_lock);  //파일 락 해제
            }
        }
        else if (victim->spte->type == VM_ANON)  //익명 페이지
        {
            victim->spte->swap_table = swap_out(victim->kaddr);  //swap 영역에 저장
        }

        victim->spte->is_loaded = false;  //페이지가 메모리에 없음을 표시
        __free_frame(victim);  //프레임 해제 처리
        lock_release(&frame_table_lock);  //락 해제

        new_frame->kaddr = palloc_get_page(flag);  //다시 프레임 할당 시도
    }

    insert_frame(new_frame);  //새 프레임을 프레임 테이블에 등록
    return new_frame;  //할당된 프레임 반환
}


void free_frame(void *kaddr)
{
    struct frame *target_frame = NULL;  //해제 대상 프레임 포인터
    struct list_elem *e;  //리스트 탐색용 변수

    lock_acquire(&frame_table_lock);  //프레임 테이블 접근을 위한 락 획득

    e = list_begin(&frame_table);  //리스트 시작 위치로 초기화
    while (e != list_end(&frame_table))  //리스트 끝까지 순회
    {
        struct frame *f = list_entry(e, struct frame, elem);  //리스트 엔트리를 frame 구조체로 변환
        if (f->kaddr == kaddr)  //kaddr와 일치하는 프레임인지 확인
        {
            target_frame = f;  //해제할 프레임 저장
            break;  //탐색 종료
        }
        e = list_next(e);  //다음 리스트 요소로 이동
    }

    if (target_frame != NULL)  //프레임을 찾은 경우
        __free_frame(target_frame);  //실제 프레임 해제 수행

    lock_release(&frame_table_lock);  //프레임 테이블 락 해제
}



static void __free_frame(struct frame *f)
{
    ASSERT(lock_held_by_current_thread(&frame_table_lock));  //락 보유 상태 확인

    pagedir_clear_page(f->t->pagedir, f->spte->vaddr);  //페이지 테이블에서 매핑 제거
    delete_frame(f);  //프레임 테이블에서 해당 엔트리 삭제
    palloc_free_page(f->kaddr);  //물리 프레임 반납
    free(f);  //frame 구조체 자체 메모리 해제
}


void swap_in(size_t used_index, void *kaddr)
{
    size_t sector_idx = used_index * 8;  //슬롯 번호 * 8 = 실제 디스크 섹터 위치
    void *buffer = kaddr;  //데이터를 적을 물리 프레임 시작 주소
    int i;  //반복 변수

    lock_acquire(&swap_lock);  //swap 영역 보호 락
    lock_acquire(&file_lock);  //블록 디바이스 보호 락

    for (i = 0; i < 8; i++)  //1페이지 = 8섹터 읽기
    {
        block_read(swap_block, sector_idx, buffer);  //swap 디스크에서 512바이트 읽기
        sector_idx++;  //다음 섹터 이동
        buffer += BLOCK_SECTOR_SIZE;  //다음 메모리 위치로 이동
    }

    bitmap_set(swap_bitmap, used_index, 0);  //해당 슬롯을 free 상태로 표시

    lock_release(&file_lock);  //파일 락 해제
    lock_release(&swap_lock);  //swap 락 해제
}


size_t swap_out(void *kaddr)
{
    size_t free_slot;  //사용 가능한 swap 슬롯 번호
    size_t sector_idx;  //디스크 첫 섹터 번호
    void *buffer = kaddr;  //프레임 데이터 시작 주소
    int i;  //반복 변수

    lock_acquire(&swap_lock);  //swap 보호 락 획득
    lock_acquire(&file_lock);  //블록 디바이스 보호 락 획득

    free_slot = bitmap_scan_and_flip(swap_bitmap, 0, 1, 0);  //빈 슬롯 하나 찾고 사용 표시
    if (free_slot == BITMAP_ERROR || free_slot >= swap_size)  //슬롯 없음 -> 실패
    {
        lock_release(&file_lock);  //파일 락 해제
        lock_release(&swap_lock);  //swap 락 해제
        return BITMAP_ERROR;  //실패 반환
    }

    sector_idx = free_slot * 8;  //슬롯 번호 → 실제 디스크 섹터 번호

    for (i = 0; i < 8; i++)  //페이지 전체(8섹터) 기록
    {
        block_write(swap_block, sector_idx, buffer);  //512바이트씩 기록
        sector_idx++;  //다음 섹터 이동
        buffer += BLOCK_SECTOR_SIZE;  //다음 메모리 위치로 이동
    }

    lock_release(&file_lock);  //파일 락 해제
    lock_release(&swap_lock);  //swap 락 해제

    return free_slot;  //swap 슬롯 번호 반환
}


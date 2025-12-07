#include "vm/page.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "userprog/pagedir.h"
#include <string.h>
#include "vm/frame.h"

extern struct lock file_lock;  //파일 시스템 접근 보호용 전역 락

static unsigned page_hash_func(const struct hash_elem *e, void *aux);  //SPT 해시 함수 선언
static bool page_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux);  //SPT 비교 함수 선언

static unsigned page_hash_func(const struct hash_elem *e, void *aux UNUSED)
{
    return hash_int(hash_entry(e, struct page, helem)->vaddr);  //page의 vaddr을 기반으로 해시값 반환
}


static bool page_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
    if (hash_entry(a, struct page, helem)->vaddr < hash_entry(b, struct page, helem)->vaddr)  //vaddr 비교
        return true;  //정렬 기준: 작은 주소가 먼저
    else
        return false;  //a가 크거나 같으면 false
}


void page_init(struct hash *page)
{
    hash_init(page, page_hash_func, page_less_func, NULL);  //SPT 해시 테이블 초기화
}


bool insert_page(struct hash *page, struct page *page_entry)
{
    struct hash_elem *result = hash_insert(page, &page_entry->helem);  //SPT에 새로운 엔트리 삽입
    return result == NULL;  //이미 존재하지 않아 삽입 성공한 경우 true
}


struct page *find_spte(void *vaddr)
{
    struct hash *page = &thread_current()->spt;  //현재 스레드의 SPT 선택
    struct page spte;  //찾기 위한 임시 page 구조체
    struct hash_elem *elem;  //검색 결과 저장용

    spte.vaddr = pg_round_down(vaddr);  //주소를 페이지 시작 주소로 정렬
    elem = hash_find(page, &spte.helem);  //SPT에서 검색

    if (elem)  //검색 성공 시
    {
        return hash_entry(elem, struct page, helem);  //page 구조체로 변환하여 반환
    }
    else  //검색 실패 시
    {
        return NULL;  //NULL 반환
    }
}


bool delete_page(struct hash *page, struct page *page_entry)
{
    if (!hash_delete(page, &page_entry->helem))  //SPT에서 제거 시도
        return false;  //삭제 실패 시 false 반환

    free_frame(pagedir_get_page(thread_current()->pagedir, page_entry->vaddr));  //해당 페이지가 점유한 프레임 해제
    free(page_entry);  //SPT 엔트리 메모리 해제
    return true;  //삭제 성공
}


void page_destroy(struct hash *page)
{
    hash_destroy(page, page_destroy_func);  //SPT 전체를 순회하며 page_destroy_func 호출
}


void page_destroy_func(struct hash_elem *e, void *aux UNUSED)
{
    struct page *p = hash_entry(e, struct page, helem);  //hash_elem → struct page 변환

    if (p == NULL)  //NULL 안전 처리
        return;

    if (p->is_loaded)  //해당 페이지가 프레임을 가지고 있다면
    {
        free_frame(pagedir_get_page(thread_current()->pagedir, p->vaddr));  //프레임 해제
    }

    free(p);  //page 엔트리 자체 메모리 해제
}


bool load_file(void *kaddr, struct page *spte)
{
    if (lock_held_by_current_thread(&file_lock))  //이미 file_lock을 보유 중인 경우
    {
        if (file_read_at(spte->file, kaddr, spte->read_bytes, spte->offset) != spte->read_bytes)  //파일 읽기 실패 시
            return false;  //실패 반환
        return true;  //성공
    }
    else  //file_lock을 잡고 있지 않은 경우
    {
        lock_acquire(&file_lock);  //파일 시스템 보호를 위해 락 획득
        if (file_read_at(spte->file, kaddr, spte->read_bytes, spte->offset) != spte->read_bytes)  //읽기 시도
        {
            lock_release(&file_lock);  //실패 시 락 해제 후 반환
            return false;  //실패
        }
        lock_release(&file_lock);  //성공 시 락 해제
        return true;  //성공
    }
}

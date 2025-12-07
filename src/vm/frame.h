#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "vm/page.h"
#include "threads/thread.h"
#include "threads/palloc.h"

struct frame  //프레임 테이블 엔트리 구조체
{
    void *kaddr;  //프레임의 커널 주소
    struct page *spte;  //이 프레임을 사용하는 가상 페이지 정보
    struct thread *t;  //이 프레임을 소유한 스레드
    struct list_elem elem;  //프레임을 프레임 테이블에 넣을 때 사용하는 연결 노드
};

void frame_init (void);  //프레임 테이블을 초기화하는 함수
void insert_frame (struct frame *frame);  //프레임을 frame_list에 추가하는 함수
void delete_frame (struct frame *frame);  //프레임을 삭제하는 함수
struct frame *alloc_frame (enum palloc_flags flag);  //물리 프레임을 할당하는 함수
void free_frame (void *kaddr);  //물리 프레임을 할당 해제하는 함수

void swap_init(void);  //스왑 테이블을 초기화하는 함수
void swap_in(size_t used_index, void* kaddr);  //페이지 내용을 스왑 영역에서 프레임으로 가져오는 함수
size_t swap_out(void* kaddr);  //페이지 내용을 프레임에서 스왑 영역으로 내보내는 함수

#endif
#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include <hash.h>

#define VM_BIN 0
#define VM_FILE 1
#define VM_ANON 2

struct page  //보조 페이지 테이블 엔트리 구조체
{
    int type;  //이 페이지가 어떤 종류의 페이지인지 나타내는 값
    void *vaddr;  //이 페이지가 관리하는 가상 페이지의 시작 주소
    bool write_enable;  //이 페이지의 쓰기 가능 여부를 나타내는 플래그
    struct file* file;  //이 페이지의 백킹 스토어가 되는 파일 포인터
    struct list_elem mmap_elem;  //이 페이지를 spte_list에 연결하기 위한 연결 노드
    bool is_loaded;  //이 페이지가 현재 물리 프레임에 올라와 있는지 나타내는 플래그
    size_t offset;  //file 안에서 이 페이지가 데이터를 읽는 시작 위치
    size_t read_bytes;  //파일에서 실제로 읽어와야 하는 바이트 수
    size_t zero_bytes;  //페이지 끝까지 남은 부분 중 0으로 채워야 하는 바이트 수
    size_t swap_table;  //이 페이지가 swap 영역에 나가 있을 때 사용하는 swap slot 번호
    struct hash_elem helem;  //이 페이지를 해시 테이블에 넣을 때 사용하는 연결 노드
    bool pinned;  //페이지 교체 시 내보내면 안 되는 페이지인지 표시하는 플래그
};

struct mmap_file  //mmap() 호출로 생기는 매핑 영역 구조체
{
    struct file* file;  //이 mmap 영역이 매핑하고 있는 실제 파일 포인터
    unsigned map_id;  //mmap() 호출 시 반환하는 고유 ID
    struct list_elem elem;  //현재 스레드의 mmap_list에 이 mmap 영역을 넣을 때 쓰는 연결 노드
    struct list spte_list;  //이 mmap 영역에 속한 모든 페이지들을 모아 둔 리스트
};

void page_init (struct hash *page);  //스레드의 SPT를 hash table로 초기화하는 함수
bool insert_page (struct hash *page, struct page *page_entry);  //SPT에 새로운 페이지 엔트리를 삽입하는 함수
bool delete_page (struct hash *page, struct page *page_entry);  //SPT에서 특정 페이지 엔트리를 제거하는 함수
struct page *find_spte (void *vaddr);  //주어진 가상 주소에 해당하는 SPT 엔트리를 찾는 함수
void page_destroy (struct hash *page);  //SPT 전체를 파괴하는 함수
void page_destroy_func (struct hash_elem *e, void *aux);  //hash_destroy()에서 각 엔트리를 삭제할 때 부르는 콜백 함수

#endif

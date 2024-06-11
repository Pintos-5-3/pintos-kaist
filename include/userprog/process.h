#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd(const char *file_name);
tid_t process_fork(const char *name, struct intr_frame *if_);
int process_exec(void *f_name);
int process_wait(tid_t);
void process_exit(void);
void process_activate(struct thread *next);

/* NOTE: [2.4] 파일 디스크립터 관련 함수 원형 */
int process_add_file(struct file *f);
struct file *process_get_file(int fd);
void process_close_file(int fd);

struct lazy_load_aux {  // load_segment -> lazy_load_segment로 넘길 인자 구조체
    struct file *file;          // 내용이 담긴 파일
    off_t ofs;                  // 파일에서 읽기 시작할 위치
    uint32_t read_bytes;        // 페이지에서 읽어야하는 바이트 수
    uint32_t zero_bytes;        // 페이지에서 읽고 난 후 남은 공간으로, 0으로 채워야 하는 바이트 수
};

static bool
lazy_load_segment(struct page *page, void *aux);

#endif /* userprog/process.h */

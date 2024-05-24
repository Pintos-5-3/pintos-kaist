#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init(void);

/* NOTE: [2.4] File에 대한 동시 접근을 막기 위한 filesys_lock 추가 */
struct lock filesys_lock;

#endif /* userprog/syscall.h */

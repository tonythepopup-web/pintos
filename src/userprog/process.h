#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/interrupt.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
void argument_passing(int argc, char **argv, struct intr_frame *_if);  //유저 스택을 C 언어 호출 형식에 맞게 세팅하는 함수

#endif /* userprog/process.h */

#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

void process_set_exit_code (int exit_code);

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

unsigned child_status_hash_func (const struct hash_elem *e, void *aux);
bool child_status_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux);

#endif /* userprog/process.h */
